/// Python to GOTO converter — Call (PLR §6.3.4)
/// implementation. The single largest cohesive block of
/// the converter: dispatches builtins, @c_intrinsic
/// library functions, class instantiation, method calls,
/// and user-defined-function calls.
///
/// Extracted from python_converter.cpp to reduce the main file size.
/// All logic and class-member state remains unchanged — this file is
/// a pure source-split.

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/config.h>
#include <util/cprover_prefix.h>
#include <util/floatbv_expr.h>
#include <util/ieee_float.h>
#include <util/json.h>
#include <util/mathematical_expr.h>
#include <util/mathematical_types.h>
#include <util/pointer_expr.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/std_types.h>
#include <util/string_constant.h>
#include <util/string_expr.h>
#include <util/symbol.h>

#include <solvers/strings/python_regex_to_smt.h>

#include "python_converter.h"
#include "python_converter_helpers.h"
#include "python_types.h"
#include "python_value_type.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <sstream>

// PLR §6.3.4: Calls
// "A call calls a callable object (e.g., a function) with a possibly
// empty series of arguments."
exprt python_convertert::convert_call(const jsont &expr)
{
  const jsont &func = json_member(expr, "func");
  const jsont &args = json_member(expr, "args");

  // Stage 1 of the re-precision plan: detect user-visible
  // regex call patterns whose pattern is a constant non-ε
  // accepting regex AND whose subject is statically the empty
  // string. Emit a 'regex-no-match' property at the call site
  // independently of the subsequent dispatch.
  //
  // Patterns recognised:
  //   re.search(p, s) / re.match / re.fullmatch
  //   compile(p).search(s) / .match / .fullmatch
  //   re.compile(p).search(s) / .match / .fullmatch
  // where p is a constant string and s is a constant string,
  // a Name with a recorded string constant, or a one-level
  // dict subscript dict_lit['K'] with a recorded string value.
  {
    auto literal_string_from_ast =
      [&](const jsont &n) -> std::optional<std::string>
    {
      if(is_node_type(n, "Constant"))
      {
        const jsont &v = json_member(n, "value");
        if(v.is_string())
          return v.value;
      }
      if(is_node_type(n, "Name"))
      {
        std::string nm = json_string(json_member(n, "id"));
        irep_idt sid{qualify_name(nm)};
        auto si = string_constants.find(sid);
        if(si != string_constants.end())
          return si->second;
      }
      if(is_node_type(n, "Subscript"))
      {
        const jsont &v_node = json_member(n, "value");
        const jsont &s_node = json_member(n, "slice");
        if(!is_node_type(s_node, "Constant"))
          return std::nullopt;
        const jsont &k = json_member(s_node, "value");
        if(!k.is_string())
          return std::nullopt;
        if(is_node_type(v_node, "Name"))
        {
          std::string nm = json_string(json_member(v_node, "id"));
          irep_idt did{qualify_name(nm)};
          auto si = dict_literal_value_string_consts.find(did);
          if(si == dict_literal_value_string_consts.end())
            return std::nullopt;
          auto ki = si->second.find(k.value);
          if(ki == si->second.end())
            return std::nullopt;
          return ki->second;
        }
      }
      return std::nullopt;
    };

    auto detect_regex_method = [&](const jsont &n) -> const char *
    {
      if(!is_node_type(n, "Attribute"))
        return nullptr;
      std::string at = json_string(json_member(n, "attr"));
      if(at == "search" || at == "match" || at == "fullmatch")
        return "search-like";
      return nullptr;
    };

    std::optional<std::string> pat_lit;
    std::optional<std::string> subj_lit;
    bool is_regex_call = false;

    if(is_node_type(func, "Attribute") && detect_regex_method(func))
    {
      const jsont &recv = json_member(func, "value");
      // Form A: re.<method>(p, s).
      if(
        is_node_type(recv, "Name") &&
        json_string(json_member(recv, "id")) == "re" && args.is_array() &&
        as_array(args).size() >= 2)
      {
        auto ai = as_array(args).begin();
        pat_lit = literal_string_from_ast(*ai);
        ++ai;
        subj_lit = literal_string_from_ast(*ai);
        is_regex_call = true;
      }
      // Form B: compile(p).<method>(s) or re.compile(p).<method>(s).
      else if(is_node_type(recv, "Call"))
      {
        const jsont &cf = json_member(recv, "func");
        bool is_compile = false;
        if(
          is_node_type(cf, "Name") &&
          json_string(json_member(cf, "id")) == "compile")
          is_compile = true;
        else if(
          is_node_type(cf, "Attribute") &&
          json_string(json_member(cf, "attr")) == "compile")
        {
          const jsont &cv = json_member(cf, "value");
          if(
            is_node_type(cv, "Name") &&
            json_string(json_member(cv, "id")) == "re")
            is_compile = true;
        }
        if(is_compile)
        {
          const jsont &cargs = json_member(recv, "args");
          if(cargs.is_array() && !as_array(cargs).empty())
            pat_lit = literal_string_from_ast(*as_array(cargs).begin());
          if(args.is_array() && !as_array(args).empty())
            subj_lit = literal_string_from_ast(*as_array(args).begin());
          is_regex_call = true;
        }
      }
    }

    if(
      is_regex_call && pat_lit.has_value() && subj_lit.has_value() &&
      subj_lit->empty())
    {
      auto can_match_empty = python_regex_can_match_empty(*pat_lit);
      if(can_match_empty.has_value() && !*can_match_empty)
      {
        source_locationt rloc = get_location(expr);
        rloc.set_property_class("regex-no-match");
        rloc.set_comment("regex '" + *pat_lit + "' cannot match empty string");
        code_assertt rne{false_exprt{}};
        rne.add_source_location() = rloc;
        code_blockt rne_block;
        rne_block.add(std::move(rne));
        pending_checks.push_back(std::move(rne_block));
      }
    }
  }

  std::string func_name;
  if(is_node_type(func, "Name"))
    func_name = json_string(json_member(func, "id"));
  else if(is_node_type(func, "Attribute"))
  {
    std::string method_name = json_string(json_member(func, "attr"));

    // PLR §6.4.6 / Python re module semantics:
    //   re.Pattern.{search,match,fullmatch} require the subject
    //   argument to be a string (or bytes-like). When the static
    //   type of the first positional argument is concretely not
    //   a string (e.g. a dict, list, set, or class instance),
    //   Python raises TypeError. We emit a dedicated ASSERT
    //   false at the call site so the bug is reported even when
    //   the subsequent regex-result modeling is a sound nondet
    //   over-approximation that would otherwise hide it.
    //
    //   The check runs BEFORE the regular method-dispatch
    //   machinery so it covers Pattern.search invoked on any
    //   receiver — both library Pattern instances and the
    //   stub-context fallback further below. Property class
    //   'type-error' is not suppressed by
    //   --python-no-exception-checks (which is for runtime
    //   exception-property suppression; this is a statically
    //   provable bug).
    if(
      method_name == "search" || method_name == "match" ||
      method_name == "fullmatch")
    {
      const jsont &args_n = json_member(expr, "args");
      if(args_n.is_array() && !as_array(args_n).empty())
      {
        // The subject is the first positional argument of the
        // call expr (the JSON args list of attribute calls
        // doesn't include the receiver).
        exprt subj = convert_expression(*as_array(args_n).begin());
        auto looks_like_string = [&](const typet &t)
        {
          if(is_python_string_type(t))
            return true;
          if(is_python_value_type(t))
            return true; // tagged union — could carry a string
          if(t.id() == ID_pointer)
            return true; // bytes / char pointer surrogate
          return false;
        };
        if(!subj.is_nil() && !looks_like_string(subj.type()))
        {
          source_locationt tloc = get_location(expr);
          tloc.set_property_class("type-error");
          tloc.set_comment(
            "re." + method_name + "() argument must be string or bytes");
          code_assertt te{false_exprt{}};
          te.add_source_location() = tloc;
          code_blockt te_block;
          te_block.add(std::move(te));
          pending_checks.push_back(std::move(te_block));
        }
      }
    }

    // PLR §6.3.4: super() — resolve to parent class
    const jsont &obj_node = json_member(func, "value");
    if(
      is_node_type(obj_node, "Call") &&
      is_node_type(json_member(obj_node, "func"), "Name") &&
      json_string(json_member(json_member(obj_node, "func"), "id")) == "super")
    {
      // PLR §3.3.2.1 C3 linearization for super() dispatch.
      // class_mro[root_class] is computed at ClassDef time.
      // mro_root_class tracks the dispatch root (the class
      // whose method invocation started the current super
      // chain) — preserved across nested super() inlining so
      // every hop consults the same MRO.
      if(!current_class.empty())
      {
        // Diagnostic recursion guard (stack-overflow safety).
        static thread_local std::size_t super_depth = 0;
        struct guardt
        {
          ~guardt()
          {
            super_depth--;
          }
        } gd;
        super_depth++;
        if(super_depth > 64)
          return side_effect_expr_nondett{
            python_int_type(), get_location(expr)};
        // Compute the next class in the MRO chain.
        std::string root =
          mro_root_class.empty() ? current_class : mro_root_class;
        std::string base_class;
        auto mit = class_mro.find(root);
        if(mit != class_mro.end())
        {
          const auto &mro = mit->second;
          for(std::size_t i = 0; i + 1 < mro.size(); ++i)
          {
            if(mro[i] == current_class)
            {
              base_class = mro[i + 1];
              break;
            }
          }
        }
        // Fallback: first declared base (single-inheritance
        // heuristic) when MRO lookup fails.
        if(
          base_class.empty() && class_bases.count(current_class) &&
          !class_bases[current_class].empty())
          base_class = class_bases[current_class][0];
        if(!base_class.empty())
        {
          // Inline super().__init__() by re-converting the base class's
          // __init__ body with the current self pointer. This avoids
          // pointer type mismatches (Derived* vs Base*).
          // Find the base class __init__ AST
          const jsont &module_body = json_member(parse_tree.ast_json, "body");
          if(module_body.is_array())
          {
            for(const auto &top_stmt : as_array(module_body))
            {
              if(
                is_node_type(top_stmt, "ClassDef") &&
                json_string(json_member(top_stmt, "name")) == base_class)
              {
                const jsont &cls_body = json_member(top_stmt, "body");
                if(cls_body.is_array())
                {
                  for(const auto &item : as_array(cls_body))
                  {
                    if(
                      (is_node_type(item, "FunctionDef") ||
                       is_node_type(item, "AsyncFunctionDef")) &&
                      json_string(json_member(item, "name")) == method_name)
                    {
                      // Convert the base __init__ body statements
                      // in the current scope (so self refers to Derived).
                      // Collect into a local buffer first, because
                      // convert_statement() clears the shared
                      // pending_checks on entry — if we push directly
                      // into pending_checks, each statement clobbers
                      // whatever the previous statement added.
                      const jsont &init_body = json_member(item, "body");
                      if(init_body.is_array())
                      {
                        std::string saved_class = current_class;
                        std::string saved_mro_root = mro_root_class;
                        if(mro_root_class.empty())
                          mro_root_class = saved_class;
                        current_class = base_class;
                        std::vector<codet> inlined;
                        for(const auto &s : as_array(init_body))
                          inlined.push_back(convert_statement(s));
                        current_class = saved_class;
                        mro_root_class = saved_mro_root;
                        for(auto &st : inlined)
                          pending_checks.push_back(std::move(st));
                      }
                      // Return a no-op value (the side effects are in
                      // pending_checks)
                      return from_integer(0, python_int_type());
                    }
                  }
                }
                break;
              }
            }
          }
        }
      }
      return side_effect_expr_nondett{python_int_type(), get_location(expr)};
    }

    // Check if this is a module function call: math.sqrt(x)
    if(is_node_type(obj_node, "Name"))
    {
      std::string obj_name = json_string(json_member(obj_node, "id"));
      if(imported_modules.count(obj_name))
      {
        // Resolve module.func to the function symbol. If the
        // resolved function is decorated with @c_intrinsic,
        // fall through to the main convert_call path so the
        // decorator's fold/domain/range semantics apply. A
        // direct function-call emission would bypass those.
        irep_idt func_id{"python::" + method_name};
        const symbolt *sym = symbol_table.lookup(func_id);
        if(
          sym != nullptr && sym->type.id() == ID_code &&
          c_intrinsic_map.count(func_id) == 0)
        {
          const code_typet &ft = to_code_type(sym->type);
          exprt::operandst arguments;
          if(args.is_array())
          {
            for(const auto &arg : as_array(args))
              arguments.push_back(convert_expression(arg));
          }
          for(std::size_t i = 0;
              i < arguments.size() && i < ft.parameters().size();
              i++)
          {
            if(arguments[i].type() != ft.parameters()[i].type())
              arguments[i] =
                safe_typecast(arguments[i], ft.parameters()[i].type());
          }
          side_effect_expr_function_callt call{
            sym->symbol_expr(),
            std::move(arguments),
            ft.return_type(),
            get_location(expr)};
          return std::move(call);
        }
        // PLR stdlib: math module functions — the attribute-style
        // math.X(...) form has its own inline handler below. The
        // bare-name 'from math import X; X(...)' form is handled
        // via the @c_intrinsic decorators in library/math.py.
        if(obj_name == "math")
        {
          // Math constants
          if(
            method_name == "pi" || method_name == "e" || method_name == "inf" ||
            method_name == "nan" || method_name == "tau")
          {
            if(method_name == "pi")
              return double_to_floatbv(M_PI);
            if(method_name == "e")
              return double_to_floatbv(M_E);
            if(method_name == "tau")
              return double_to_floatbv(2.0 * M_PI);
            if(method_name == "inf")
            {
              ieee_floatt val{
                ieee_float_spect::double_precision(),
                ieee_floatt::rounding_modet::ROUND_TO_EVEN};
              val.make_plus_infinity();
              return val.to_expr();
            }
            // nan
            {
              ieee_floatt val{
                ieee_float_spect::double_precision(),
                ieee_floatt::rounding_modet::ROUND_TO_EVEN};
              val.make_NaN();
              return val.to_expr();
            }
          }
          // Dispatch with the math function name. The attribute
          // form uses the following inline block (exact models
          // for ceil/floor/fabs/isclose; domain-checked folding;
          // nondet-with-constraints for symbolic args). A future
          // cleanup will migrate this to the decorator path for
          // parity with the bare-name form.
          std::string saved_func = func_name;
          func_name = method_name;
          exprt math_arg =
            args.is_array() && !as_array(args).empty()
              ? convert_expression(*as_array(args).begin())
              : side_effect_expr_nondett{double_type(), get_location(expr)};
          if(math_arg.type().id() != ID_floatbv)
          {
            if(math_arg.is_constant() && math_arg.type().id() == ID_signedbv)
            {
              mp_integer iv;
              if(!to_integer(to_constant_expr(math_arg), iv))
              {
                ieee_floatt fv{
                  ieee_float_spect::double_precision(),
                  ieee_floatt::rounding_modet::ROUND_TO_EVEN};
                fv.from_integer(iv);
                math_arg = fv.to_expr();
              }
              else
                math_arg = safe_typecast(math_arg, double_type());
            }
            else
              math_arg = safe_typecast(math_arg, double_type());
          }

          // Exact models
          if(func_name == "ceil")
            return plus_exprt{
              typecast_exprt{math_arg, python_int_type()},
              if_exprt{
                binary_relation_exprt{
                  typecast_exprt{
                    typecast_exprt{math_arg, python_int_type()}, double_type()},
                  ID_lt,
                  math_arg},
                from_integer(1, python_int_type()),
                from_integer(0, python_int_type())}};
          if(func_name == "floor")
            return minus_exprt{
              typecast_exprt{math_arg, python_int_type()},
              if_exprt{
                binary_relation_exprt{
                  typecast_exprt{
                    typecast_exprt{math_arg, python_int_type()}, double_type()},
                  ID_gt,
                  math_arg},
                from_integer(1, python_int_type()),
                from_integer(0, python_int_type())}};
          if(func_name == "fabs")
            return if_exprt{
              binary_relation_exprt{math_arg, ID_lt, safe_zero(double_type())},
              unary_minus_exprt{math_arg},
              math_arg};
          if(func_name == "trunc")
            return typecast_exprt{math_arg, python_int_type()};
          if(func_name == "isnan")
            return isnan_exprt{math_arg};
          if(func_name == "isinf")
            return isinf_exprt{math_arg};
          if(func_name == "isfinite")
            return and_exprt{
              not_exprt{isnan_exprt{math_arg}},
              not_exprt{isinf_exprt{math_arg}}};
          if(func_name == "copysign")
          {
            exprt arg2 =
              as_array(args).size() >= 2
                ? convert_expression(*std::next(as_array(args).begin()))
                : math_arg;
            if(arg2.type().id() != ID_floatbv)
              arg2 = safe_typecast(arg2, double_type());
            exprt abs_x = if_exprt{
              binary_relation_exprt{math_arg, ID_lt, safe_zero(double_type())},
              unary_minus_exprt{math_arg},
              math_arg};
            return if_exprt{
              binary_relation_exprt{arg2, ID_lt, safe_zero(double_type())},
              unary_minus_exprt{abs_x},
              abs_x};
          }
          if(func_name == "isclose")
          {
            exprt arg2 =
              as_array(args).size() >= 2
                ? convert_expression(*std::next(as_array(args).begin()))
                : math_arg;
            if(arg2.type().id() != ID_floatbv)
              arg2 = safe_typecast(arg2, double_type());
            ieee_floatt tol{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            tol.from_double(1e-9);
            exprt diff = if_exprt{
              binary_relation_exprt{
                minus_exprt{math_arg, arg2}, ID_lt, safe_zero(double_type())},
              unary_minus_exprt{minus_exprt{math_arg, arg2}},
              minus_exprt{math_arg, arg2}};
            return binary_relation_exprt{diff, ID_le, tol.to_expr()};
          }
          // Option-4 domain check: raise ValueError for known-bad
          // constant arguments, and emit a guarded ValueError for
          // non-constant arguments. See math_function_domain() /
          // emit_value_error() and doc/architectural/
          // python-module-support-plan.md.
          {
            auto domain_opt = math_function_domain(func_name, math_arg);
            auto pre_eval = try_eval_double(math_arg);
            if(pre_eval.has_value())
            {
              double val = pre_eval.value();
              bool in_domain = true;
              if(domain_opt.has_value())
              {
                if(func_name == "sqrt")
                  in_domain = val >= 0;
                else if(
                  func_name == "log" || func_name == "log2" ||
                  func_name == "log10")
                  in_domain = val > 0;
                else if(func_name == "log1p")
                  in_domain = val > -1;
                else if(func_name == "asin" || func_name == "acos")
                  in_domain = val >= -1 && val <= 1;
                else if(func_name == "atanh")
                  in_domain = val > -1 && val < 1;
                else if(func_name == "acosh")
                  in_domain = val >= 1;
              }
              if(!in_domain)
              {
                emit_value_error(false_exprt{});
                return side_effect_expr_nondett{
                  double_type(), get_location(expr)};
              }
              // In-domain constant — fall through to the constant
              // folding block below.
            }
            else if(domain_opt.has_value())
            {
              // Non-constant argument: emit a guarded ValueError
              // and fall through to the nondet-with-constraints
              // path below.
              emit_value_error(domain_opt.value());
            }
          }

          // Constant evaluation (handle typecast from int→float)
          {
            auto eval_result = try_eval_double(math_arg);
            if(eval_result.has_value())
            {
              double val = eval_result.value();
              double res = 0;
              bool computed = true;
              if(func_name == "sqrt" && val >= 0)
                res = std::sqrt(val);
              else if(func_name == "sin")
                res = std::sin(val);
              else if(func_name == "cos")
                res = std::cos(val);
              else if(func_name == "tan")
                res = std::tan(val);
              else if(func_name == "asin" && val >= -1 && val <= 1)
                res = std::asin(val);
              else if(func_name == "acos" && val >= -1 && val <= 1)
                res = std::acos(val);
              else if(func_name == "atan")
                res = std::atan(val);
              else if(func_name == "log" && val > 0)
                res = std::log(val);
              else if(func_name == "log2" && val > 0)
                res = std::log2(val);
              else if(func_name == "log10" && val > 0)
                res = std::log10(val);
              else if(func_name == "exp")
                res = std::exp(val);
              else if(func_name == "exp2")
                res = std::exp2(val);
              else if(func_name == "floor")
                res = std::floor(val);
              else if(func_name == "ceil")
                res = std::ceil(val);
              else if(func_name == "fabs")
                res = std::fabs(val);
              else if(func_name == "degrees")
                res = val * 180.0 / M_PI;
              else if(func_name == "radians")
                res = val * M_PI / 180.0;
              else if(func_name == "sinh")
                res = std::sinh(val);
              else if(func_name == "cosh")
                res = std::cosh(val);
              else if(func_name == "tanh")
                res = std::tanh(val);
              else if(func_name == "asinh")
                res = std::asinh(val);
              else if(func_name == "acosh" && val >= 1)
                res = std::acosh(val);
              else if(func_name == "atanh" && val > -1 && val < 1)
                res = std::atanh(val);
              else if(func_name == "erf")
                res = std::erf(val);
              else if(func_name == "erfc")
                res = std::erfc(val);
              else if(func_name == "gamma" || func_name == "lgamma")
                res = std::lgamma(val);
              else if(func_name == "cbrt")
                res = std::cbrt(val);
              else if(func_name == "expm1")
                res = std::expm1(val);
              else if(func_name == "log1p" && val > -1)
                res = std::log1p(val);
              else
                computed = false;
              if(computed)
                return double_to_floatbv(res);
            }
          }
          // Two-arg constant evaluation. Delegates to the
          // same fold table the @c_intrinsic decorator uses
          // for bare-name calls, so math.X(a, b) and X(a, b)
          // (after `from math import X`) share semantics.
          //
          // log(x, base) is the one attribute-only case not
          // in the decorator's fold map (the bare-name log
          // routes through its own single-arg fold): keep its
          // inline handling below.
          if(as_array(args).size() >= 2)
          {
            exprt arg2 = convert_expression(*std::next(as_array(args).begin()));
            if(arg2.type().id() != ID_floatbv)
              arg2 = safe_typecast(arg2, double_type());
            auto ev1 = try_eval_double(math_arg);
            auto ev2 = try_eval_double(arg2);
            if(ev1.has_value() && ev2.has_value())
            {
              double v1 = ev1.value(), v2 = ev2.value();
              if(func_name == "log" && v2 > 0 && v1 > 0)
                return double_to_floatbv(std::log(v1) / std::log(v2));
              irep_idt math_id{"python::" + func_name};
              auto fi = c_intrinsic_fold_map.find(math_id);
              if(fi != c_intrinsic_fold_map.end())
              {
                const std::string &op = fi->second;
                double r = 0;
                bool ok = true;
                if(op == "pow")
                  r = std::pow(v1, v2);
                else if(op == "atan2")
                  r = std::atan2(v1, v2);
                else if(op == "fmod")
                  r = std::fmod(v1, v2);
                else if(op == "hypot")
                  r = std::hypot(v1, v2);
                else if(op == "copysign")
                  r = std::copysign(v1, v2);
                else if(op == "remainder")
                  r = std::remainder(v1, v2);
                else
                  ok = false;
                if(ok && std::isfinite(r))
                  return double_to_floatbv(r);
              }
            }
          }
          // Nondet with constraints. Look up domain= / range=
          // from the decorator map (populated by the library's
          // @c_intrinsic annotations in math.py) so the
          // attribute-style form shares semantics with the
          // bare-name form. Previously this had its own inline
          // constraint logic duplicating the decorator path.
          {
            irep_idt math_id{"python::" + func_name};
            auto di = c_intrinsic_domain_map.find(math_id);
            auto ri = c_intrinsic_range_map.find(math_id);
            std::string dom =
              di != c_intrinsic_domain_map.end() ? di->second : std::string{};
            std::string rng =
              ri != c_intrinsic_range_map.end() ? ri->second : std::string{};
            // The sqrt-specific extra axiom (result*result == arg)
            // was a past improvement that the decorator path
            // doesn't emit; preserve it here for now.
            exprt tv = emit_math_intrinsic_nondet(
              dom, rng, math_arg, get_location(expr));
            if(func_name == "sqrt")
            {
              ieee_floatt fz{
                ieee_float_spect::double_precision(),
                ieee_floatt::rounding_modet::ROUND_TO_EVEN};
              fz.from_double(0.0);
              exprt float_arg = math_arg;
              if(math_arg.type().id() != ID_floatbv)
                float_arg = typecast_exprt(math_arg, double_type());
              exprt arg_nonneg =
                binary_relation_exprt{float_arg, ID_ge, fz.to_expr()};
              pending_checks.push_back(code_assumet{implies_exprt{
                arg_nonneg,
                ieee_float_equal_exprt{mult_exprt{tv, tv}, float_arg}}});
            }
            return tv;
          }
        }
        // PLR stdlib: re module — return nondet for all methods
        if(obj_name == "re")
          return side_effect_expr_nondett{
            python_int_type(), get_location(expr)};
        // PLib: random module — constrained nondet for randint
        if(obj_name == "random")
        {
          if(
            method_name == "randint" && args.is_array() &&
            as_array(args).size() >= 2)
          {
            auto it = as_array(args).begin();
            exprt lo = convert_expression(*it);
            ++it;
            exprt hi = convert_expression(*it);
            // Return nondet int with assume(lo <= result <= hi)
            side_effect_expr_nondett nondet{
              python_int_type(), get_location(expr)};
            static unsigned rand_ctr = 0;
            std::string tmp = "__rand_" + std::to_string(rand_ctr++);
            std::string tq = qualify_name(tmp);
            irep_idt ti{tq};
            if(symbol_table.lookup(ti) == nullptr)
            {
              symbolt ts{ti, python_int_type(), "python"};
              ts.base_name = tmp;
              ts.is_lvalue = true;
              ts.is_state_var = true;
              symbol_table.add(ts);
            }
            symbol_exprt tv = symbol_table.lookup_ref(ti).symbol_expr();
            pending_checks.push_back(code_frontend_assignt{tv, nondet});
            pending_checks.push_back(code_assumet{and_exprt{
              binary_relation_exprt{tv, ID_ge, lo},
              binary_relation_exprt{tv, ID_le, hi}}});
            return std::move(tv);
          }
          return side_effect_expr_nondett{
            python_int_type(), get_location(expr)};
        }
        return side_effect_expr_nondett{python_int_type(), get_location(expr)};
      }
    }

    // Method call: obj.method(args)
    exprt obj = convert_expression(json_member(func, "value"));
    if(obj.is_nil())
      return nil_exprt{};

    // Resolve class name from object's type (struct or pointer-to-struct)
    typet obj_base_type = obj.type();
    if(obj_base_type.id() == ID_pointer)
      obj_base_type = to_pointer_type(obj_base_type).base_type();

    if(obj_base_type.id() == ID_struct || obj_base_type.id() == ID_struct_tag)
    {
      // PLR §3.2: Complex number methods
      if(
        obj_base_type.id() == ID_struct &&
        to_struct_type(obj_base_type).get_tag() == "python_complex")
      {
        if(method_name == "conjugate")
        {
          member_exprt r{obj, "real", double_type()};
          member_exprt i{obj, "imag", double_type()};
          return struct_exprt{
            {r, unary_minus_exprt{i}}, to_struct_type(obj_base_type)};
        }
        // Other complex methods: return nondet
        return side_effect_expr_nondett{obj_base_type, get_location(expr)};
      }

      // PLib stdtypes: String methods
      if(is_python_string_type(obj_base_type))
      {
        if(method_name == "split")
        {
          // PLib stdtypes: str.split(sep) — constant optimization
          if(args.is_array() && !as_array(args).empty())
          {
            exprt delim_expr = convert_expression(*as_array(args).begin());
            auto obj_sv = extract_string_value(obj);
            auto delim_sv = extract_string_value(delim_expr);
            if(obj_sv.has_value() && delim_sv.has_value())
            {
              std::string s = obj_sv.value();
              std::string d = delim_sv.value();
              std::vector<std::string> parts;
              if(!d.empty())
              {
                std::size_t pos = 0;
                while(pos <= s.size())
                {
                  auto found = s.find(d, pos);
                  if(found == std::string::npos)
                  {
                    parts.push_back(s.substr(pos));
                    break;
                  }
                  parts.push_back(s.substr(pos, found - pos));
                  pos = found + d.size();
                }
              }
              else
                parts.push_back(s);

              struct_typet str_type = python_string_struct_def();
              typet list_type = python_list_type(python_string_type());
              const auto &data_type =
                to_array_type(to_struct_type(list_type).components()[1].type());
              exprt::operandst list_elems;
              for(const auto &p : parts)
                list_elems.push_back(python_string_literal(p));
              while(list_elems.size() < PYTHON_MAX_LIST_LENGTH)
                list_elems.push_back(safe_zero(python_string_type()));
              return struct_exprt{
                {from_integer(
                   static_cast<long long>(parts.size()), python_int_type()),
                 array_exprt{std::move(list_elems), data_type}},
                list_type};
            }
          }
          // Fallback: original struct-literal handler
          if(
            obj.id() == ID_struct && args.is_array() && !as_array(args).empty())
          {
            exprt delim_expr = convert_expression(*as_array(args).begin());
            // Extract constant string bytes from obj
            if(
              obj.operands().size() == 2 && obj.operands()[0].is_constant() &&
              delim_expr.id() == ID_struct &&
              delim_expr.operands().size() == 2 &&
              delim_expr.operands()[0].is_constant())
            {
              mp_integer slen, dlen;
              if(
                !to_integer(to_constant_expr(obj.operands()[0]), slen) &&
                !to_integer(to_constant_expr(delim_expr.operands()[0]), dlen) &&
                dlen == 1 && slen <= PYTHON_MAX_STRING_LENGTH)
              {
                // Get delimiter byte
                const auto &delim_data = delim_expr.operands()[1];
                mp_integer delim_byte;
                if(
                  delim_data.operands().size() > 0 &&
                  delim_data.operands()[0].is_constant() &&
                  !to_integer(
                    to_constant_expr(delim_data.operands()[0]), delim_byte))
                {
                  // Scan string for delimiter, build parts
                  const auto &str_data = obj.operands()[1];
                  std::vector<std::string> parts;
                  std::string current;
                  for(mp_integer i = 0; i < slen; ++i)
                  {
                    std::size_t idx = i.to_ulong();
                    if(
                      idx < str_data.operands().size() &&
                      str_data.operands()[idx].is_constant())
                    {
                      mp_integer ch;
                      if(!to_integer(
                           to_constant_expr(str_data.operands()[idx]), ch))
                      {
                        if(ch == delim_byte)
                        {
                          parts.push_back(current);
                          current.clear();
                        }
                        else
                          current += static_cast<char>(ch.to_ulong());
                      }
                    }
                  }
                  parts.push_back(current);

                  // Build list of string structs
                  struct_typet str_type = python_string_struct_def();
                  const auto &data_type = array_typet(
                    unsignedbv_typet{8},
                    from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));
                  typet list_type = python_list_type(python_string_type());
                  const auto &list_data_type = to_array_type(
                    to_struct_type(list_type).components()[1].type());

                  exprt::operandst list_elems;
                  for(const auto &part : parts)
                  {
                    list_elems.push_back(python_string_literal(part));
                  }
                  while(list_elems.size() < PYTHON_MAX_LIST_LENGTH)
                    list_elems.push_back(safe_zero(python_string_type()));
                  exprt len_expr = from_integer(
                    static_cast<long long>(parts.size()), python_int_type());
                  return struct_exprt{
                    {len_expr,
                     array_exprt{std::move(list_elems), list_data_type}},
                    list_type};
                }
              }
            }
          }
          // Fallback: nondet list of strings
          return side_effect_expr_nondett{
            python_list_type(python_string_type()), get_location(expr)};
        }
        // PLib stdtypes: upper/lower — exact byte transformation
        if(method_name == "upper" || method_name == "lower")
        {
          auto sv = extract_string_value(obj);
          if(!sv.has_value() && obj.id() == ID_symbol)
          {
            auto it =
              string_constants.find(to_symbol_expr(obj).get_identifier());
            if(it != string_constants.end())
              sv = it->second;
          }
          if(sv.has_value())
          {
            std::string result = sv.value();
            for(auto &c : result)
              c = (method_name == "upper") ? toupper(c) : tolower(c);
            return python_string_literal(result);
          }
          // Use string solver for non-constant upper/lower
          {
            // Decompose obj into struct for the solver
            exprt src =
              (obj.id() == ID_struct && obj.operands().size() == 2)
                ? obj
                : exprt(struct_exprt(
                    {member_exprt(obj, "length", signedbv_typet{64}),
                     member_exprt(
                       obj, "data", pointer_typet(unsignedbv_typet{8}, 64))},
                    obj.type()));
            return emit_string_function(
              method_name == "upper" ? ID_cprover_string_to_upper_case_func
                                     : ID_cprover_string_to_lower_case_func,
              {src},
              symbol_table,
              pending_checks);
          }
          const auto &data_type = array_typet(
            unsignedbv_typet{8},
            from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));
          member_exprt src_data{obj, "data", data_type};
          member_exprt src_len{obj, "length", signedbv_typet{64}};

          static unsigned case_ctr = 0;
          std::string tn = "__case_" + std::to_string(case_ctr++);
          std::string tq = qualify_name(tn);
          irep_idt ti{tq};
          if(symbol_table.lookup(ti) == nullptr)
          {
            symbolt ts{ti, python_string_type(), "python"};
            ts.base_name = tn;
            ts.is_lvalue = true;
            ts.is_state_var = true;
            symbol_table.add(ts);
          }
          symbol_exprt tmp = symbol_table.lookup_ref(ti).symbol_expr();
          pending_checks.push_back(code_frontend_assignt{
            member_exprt{tmp, "length", signedbv_typet{64}}, src_len});
          member_exprt dst_data{tmp, "data", data_type};

          exprt lo = from_integer(
            method_name == "upper" ? 'a' : 'A', unsignedbv_typet{8});
          exprt hi = from_integer(
            method_name == "upper" ? 'z' : 'Z', unsignedbv_typet{8});
          exprt offset =
            from_integer(method_name == "upper" ? -32 : 32, signedbv_typet{8});

          for(std::size_t i = 0; i < PYTHON_MAX_STRING_LENGTH; i++)
          {
            exprt idx = from_integer(i, signedbv_typet{64});
            exprt ch = index_exprt{src_data, idx};
            exprt in_range = and_exprt{
              binary_relation_exprt{ch, ID_ge, lo},
              binary_relation_exprt{ch, ID_le, hi}};
            exprt converted =
              plus_exprt{ch, typecast_exprt{offset, unsignedbv_typet{8}}};
            pending_checks.push_back(code_frontend_assignt{
              index_exprt{dst_data, idx}, if_exprt{in_range, converted, ch}});
          }
          return std::move(tmp);
        }
        if(
          method_name == "strip" || method_name == "lstrip" ||
          method_name == "rstrip" || method_name == "title" ||
          method_name == "capitalize" || method_name == "swapcase" ||
          method_name == "zfill" || method_name == "casefold" ||
          method_name == "center" || method_name == "ljust" ||
          method_name == "rjust" || method_name == "expandtabs" ||
          method_name == "zfill" || method_name == "encode" ||
          method_name == "decode" || method_name == "removeprefix" ||
          method_name == "removesuffix")
        {
          // Try exact computation for constant strings
          auto str_val = extract_string_value(obj);
          if(str_val.has_value())
          {
            std::string s = str_val.value();
            std::string result;
            if(
              method_name == "strip" || method_name == "lstrip" ||
              method_name == "rstrip")
            {
              std::string chars_to_strip = " \t\n\r\f\v";
              if(args.is_array() && !as_array(args).empty())
              {
                auto cv = extract_string_value(
                  convert_expression(*as_array(args).begin()));
                if(cv.has_value())
                  chars_to_strip = cv.value();
              }
              if(method_name == "strip")
              {
                size_t start = s.find_first_not_of(chars_to_strip);
                size_t end = s.find_last_not_of(chars_to_strip);
                result = (start == std::string::npos)
                           ? ""
                           : s.substr(start, end - start + 1);
              }
              else if(method_name == "lstrip")
              {
                size_t start = s.find_first_not_of(chars_to_strip);
                result = (start == std::string::npos) ? "" : s.substr(start);
              }
              else // rstrip
              {
                size_t end = s.find_last_not_of(chars_to_strip);
                result = (end == std::string::npos) ? "" : s.substr(0, end + 1);
              }
            }
            else if(method_name == "capitalize")
            {
              result = s;
              if(!result.empty())
              {
                result[0] = static_cast<char>(
                  std::toupper(static_cast<unsigned char>(result[0])));
                for(size_t i = 1; i < result.size(); i++)
                  result[i] = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(result[i])));
              }
            }
            else if(method_name == "title")
            {
              result = s;
              bool next_upper = true;
              for(size_t i = 0; i < result.size(); i++)
              {
                if(std::isalpha(static_cast<unsigned char>(result[i])))
                {
                  result[i] =
                    next_upper
                      ? static_cast<char>(
                          std::toupper(static_cast<unsigned char>(result[i])))
                      : static_cast<char>(
                          std::tolower(static_cast<unsigned char>(result[i])));
                  next_upper = false;
                }
                else
                  next_upper = true;
              }
            }
            else if(method_name == "swapcase")
            {
              result = s;
              for(auto &c : result)
              {
                if(std::isupper(static_cast<unsigned char>(c)))
                  c = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c)));
                else if(std::islower(static_cast<unsigned char>(c)))
                  c = static_cast<char>(
                    std::toupper(static_cast<unsigned char>(c)));
              }
            }
            else if(method_name == "casefold")
            {
              result = s;
              for(auto &c : result)
                c = static_cast<char>(
                  std::tolower(static_cast<unsigned char>(c)));
            }
            else if(
              method_name == "center" || method_name == "ljust" ||
              method_name == "rjust")
            {
              int width = 0;
              char fill = ' ';
              if(args.is_array() && !as_array(args).empty())
              {
                auto ait = as_array(args).begin();
                auto wv = try_eval_double(convert_expression(*ait));
                if(wv.has_value())
                  width = static_cast<int>(wv.value());
                ++ait;
                if(ait != as_array(args).end())
                {
                  auto fv = extract_string_value(convert_expression(*ait));
                  if(fv.has_value() && !fv.value().empty())
                    fill = fv.value()[0];
                }
              }
              if(static_cast<int>(s.size()) >= width)
                result = s;
              else
              {
                int pad = width - static_cast<int>(s.size());
                if(method_name == "center")
                {
                  int left_pad = pad / 2;
                  int right_pad = pad - left_pad;
                  result = std::string(left_pad, fill) + s +
                           std::string(right_pad, fill);
                }
                else if(method_name == "ljust")
                  result = s + std::string(pad, fill);
                else
                  result = std::string(pad, fill) + s;
              }
            }
            else if(method_name == "expandtabs")
            {
              int tabsize = 8;
              if(args.is_array() && !as_array(args).empty())
              {
                auto tv =
                  try_eval_double(convert_expression(*as_array(args).begin()));
                if(tv.has_value())
                  tabsize = static_cast<int>(tv.value());
              }
              result.clear();
              int col = 0;
              for(char c : s)
              {
                if(c == '\t')
                {
                  int spaces = tabsize - (col % tabsize);
                  result += std::string(spaces, ' ');
                  col += spaces;
                }
                else if(c == '\n' || c == '\r')
                {
                  result += c;
                  col = 0;
                }
                else
                {
                  result += c;
                  col++;
                }
              }
            }
            else if(method_name == "removeprefix")
            {
              std::string prefix;
              if(args.is_array() && !as_array(args).empty())
              {
                auto pv = extract_string_value(
                  convert_expression(*as_array(args).begin()));
                if(pv.has_value())
                  prefix = pv.value();
              }
              if(!prefix.empty() && s.find(prefix) == 0)
                result = s.substr(prefix.size());
              else
                result = s;
            }
            else if(method_name == "removesuffix")
            {
              std::string suffix;
              if(args.is_array() && !as_array(args).empty())
              {
                auto sv2 = extract_string_value(
                  convert_expression(*as_array(args).begin()));
                if(sv2.has_value())
                  suffix = sv2.value();
              }
              if(
                !suffix.empty() && s.size() >= suffix.size() &&
                s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0)
                result = s.substr(0, s.size() - suffix.size());
              else
                result = s;
            }
            else if(method_name == "zfill")
            {
              int width = 0;
              if(args.is_array() && !as_array(args).empty())
              {
                auto wv =
                  try_eval_double(convert_expression(*as_array(args).begin()));
                if(wv.has_value())
                  width = static_cast<int>(wv.value());
              }
              if(static_cast<int>(s.size()) >= width)
                result = s;
              else
              {
                int pad = width - static_cast<int>(s.size());
                if(!s.empty() && (s[0] == '+' || s[0] == '-'))
                  result = s[0] + std::string(pad, '0') + s.substr(1);
                else
                  result = std::string(pad, '0') + s;
              }
            }
            else
              result = s; // fallback for unhandled methods
            if(result.size() <= PYTHON_MAX_STRING_LENGTH)
              return python_string_literal(result);
          }
          // Return nondet string with constraints for symbolic strings
          {
            static unsigned str_method_ctr = 0;
            std::string tn = "__str_meth_" + std::to_string(str_method_ctr++);
            std::string tq = qualify_name(tn);
            irep_idt ti{tq};
            if(symbol_table.lookup(ti) == nullptr)
            {
              symbolt ts{ti, python_string_type(), "python"};
              ts.base_name = tn;
              ts.is_lvalue = true;
              ts.is_state_var = true;
              symbol_table.add(ts);
            }
            symbol_exprt tv = symbol_table.lookup_ref(ti).symbol_expr();
            pending_checks.push_back(code_frontend_assignt{
              tv,
              side_effect_expr_nondett{
                python_string_type(), get_location(expr)}});
            // Both ASSUMEs use cprover_string_length_func so the
            // len()-consumer (which also goes through the intrinsic)
            // sees a value coordinated with the input's length.
            // The .length member-access path remains valid too
            // because refine-strings' array_pool.find() respects
            // the struct's .length field for non-constant
            // pointers.
            exprt result_len_intr = emit_string_int_function(
              ID_cprover_string_length_func, tv, symbol_table, pending_checks);
            exprt input_len_intr = emit_string_int_function(
              ID_cprover_string_length_func, obj, symbol_table, pending_checks);
            // strip/lstrip/rstrip: result length <= input length
            if(
              method_name == "strip" || method_name == "lstrip" ||
              method_name == "rstrip")
            {
              pending_checks.push_back(code_assumet{and_exprt{
                binary_relation_exprt{
                  result_len_intr, ID_ge, from_integer(0, signedbv_typet{64})},
                binary_relation_exprt{
                  result_len_intr, ID_le, input_len_intr}}});
            }
            // capitalize/title/swapcase/casefold: same length
            else if(
              method_name == "capitalize" || method_name == "title" ||
              method_name == "swapcase" || method_name == "casefold")
            {
              pending_checks.push_back(
                code_assumet{equal_exprt{result_len_intr, input_len_intr}});
            }
            return std::move(tv);
          }
        }
        if(method_name == "replace" || method_name == "format")
        {
          // PLib stdtypes: str.replace(old, new) for constant strings
          if(
            method_name == "replace" && args.is_array() &&
            as_array(args).size() >= 2)
          {
            auto ait = as_array(args).begin();
            exprt old_expr = convert_expression(*ait);
            ++ait;
            exprt new_expr = convert_expression(*ait);
            // Extract all three as constant strings
            auto extract_str = [&](const exprt &e) -> std::string
            {
              auto sv = extract_string_value(e);
              if(sv.has_value())
                return sv.value();
              return "";
            };
            std::string src = extract_str(obj);
            std::string old_s = extract_str(old_expr);
            std::string new_s = extract_str(new_expr);
            if(!src.empty() && !old_s.empty())
            {
              // Perform replacement
              std::string result;
              std::size_t pos = 0;
              while(pos < src.size())
              {
                auto found = src.find(old_s, pos);
                if(found == std::string::npos)
                {
                  result += src.substr(pos);
                  break;
                }
                result += src.substr(pos, found - pos) + new_s;
                pos = found + old_s.size();
              }
              // Build string literal
              return python_string_literal(result);
            }
            // Symbolic-string replace not wired: the solver's
            // cprover_string_replace_func handles char-to-char
            // replacement only (5 args, chars not strings).
            // str.replace(old, new) on general strings needs
            // a new multi-char solver intrinsic. Falls through
            // to nondet for now.
          }
          // PLib stdtypes: str.format() — substitute {} placeholders
          if(method_name == "format")
          {
            auto fmt_sv = extract_string_value(obj);
            if(fmt_sv.has_value())
            {
              std::string fmt = fmt_sv.value();
              // Simple {} substitution with string args
              std::string result;
              std::size_t arg_idx = 0;
              exprt::operandst arg_exprs;
              if(args.is_array())
              {
                for(const auto &a : as_array(args))
                  arg_exprs.push_back(convert_expression(a));
              }
              bool all_const = true;
              for(std::size_t i = 0; i < fmt.size(); i++)
              {
                if(fmt[i] == '{' && i + 1 < fmt.size())
                {
                  // Find closing }
                  auto close = fmt.find('}', i + 1);
                  if(close == std::string::npos)
                  {
                    result += fmt[i];
                    continue;
                  }
                  std::string spec = fmt.substr(i + 1, close - i - 1);
                  i = close; // skip to }

                  // Determine which arg to use
                  std::size_t use_idx = arg_idx;
                  std::string fmt_spec;
                  if(spec.empty())
                  {
                    // {} — use next arg
                    use_idx = arg_idx++;
                  }
                  else if(std::isdigit(spec[0]))
                  {
                    // {0}, {1}, {0:d}, etc.
                    auto colon = spec.find(':');
                    use_idx = std::stoul(spec.substr(
                      0, colon != std::string::npos ? colon : spec.size()));
                    if(colon != std::string::npos)
                      fmt_spec = spec.substr(colon + 1);
                  }
                  else if(spec[0] == ':')
                  {
                    // {:d}, {:.2f}, etc.
                    fmt_spec = spec.substr(1);
                    use_idx = arg_idx++;
                  }
                  else
                  {
                    all_const = false;
                    continue;
                  }

                  if(use_idx >= arg_exprs.size())
                  {
                    all_const = false;
                    continue;
                  }

                  // Extract value
                  auto sv = extract_string_value(arg_exprs[use_idx]);
                  if(sv.has_value())
                  {
                    result += sv.value();
                  }
                  else
                  {
                    auto fv = try_eval_double(arg_exprs[use_idx]);
                    if(fv.has_value())
                    {
                      double d = fv.value();
                      if(fmt_spec.empty() || fmt_spec == "d" || fmt_spec == "n")
                      {
                        if(d == std::floor(d) && std::abs(d) < 1e15)
                          result += std::to_string(static_cast<long long>(d));
                        else
                        {
                          std::ostringstream oss;
                          oss << d;
                          result += oss.str();
                        }
                      }
                      else if(fmt_spec[0] == '.')
                      {
                        // .Nf format
                        int prec = std::stoi(fmt_spec.substr(1));
                        std::ostringstream oss;
                        oss << std::fixed << std::setprecision(prec) << d;
                        result += oss.str();
                      }
                      else
                        all_const = false;
                    }
                    else
                      all_const = false;
                  }
                }
                else
                  result += fmt[i];
              }
              if(all_const)
              {
                return python_string_literal(result);
              }
              // Symbolic int fast path: template is just "{}"
              // or "{0}" with no format spec, single int arg.
              // Emit cprover_string_of_int_func.
              if(
                (fmt == "{}" || fmt == "{0}") && arg_exprs.size() == 1 &&
                (arg_exprs[0].type().id() == ID_signedbv ||
                 arg_exprs[0].type().id() == ID_integer))
              {
                exprt as_i64 =
                  arg_exprs[0].type() == signedbv_typet{64}
                    ? arg_exprs[0]
                    : safe_typecast(arg_exprs[0], signedbv_typet{64});
                exprt r = emit_string_function(
                  ID_cprover_string_of_int_func,
                  {as_i64},
                  symbol_table,
                  pending_checks);
                auto ensure_fn = [&](const irep_idt &fid)
                {
                  if(symbol_table.lookup(fid) == nullptr)
                  {
                    array_typet inf_array_type{
                      unsignedbv_typet{8}, infinity_exprt(signedbv_typet{64})};
                    std::vector<typet> at;
                    if(fid == ID_cprover_associate_array_to_pointer_func)
                    {
                      at.push_back(inf_array_type);
                      at.push_back(pointer_typet(unsignedbv_typet{8}, 64));
                    }
                    else
                    {
                      at.push_back(inf_array_type);
                      at.push_back(signedbv_typet{64});
                    }
                    symbolt fs{
                      fid,
                      mathematical_function_typet(
                        std::move(at), signedbv_typet{32}),
                      "python"};
                    fs.base_name = id2string(fid);
                    symbol_table.add(fs);
                  }
                };
                ensure_fn(ID_cprover_associate_array_to_pointer_func);
                ensure_fn(ID_cprover_associate_length_to_array_func);
                return r;
              }
            }
          }
          return side_effect_expr_nondett{
            python_string_type(), get_location(expr)};
        }
        // PLib stdtypes: startswith/endswith — exact byte comparison
        if(
          (method_name == "startswith" || method_name == "endswith") &&
          args.is_array() && !as_array(args).empty())
        {
          exprt prefix = convert_expression(*as_array(args).begin());
          // Constant-string optimization
          auto obj_sv = extract_string_value(obj);
          auto pre_sv = extract_string_value(prefix);
          if(obj_sv.has_value() && pre_sv.has_value())
          {
            bool result;
            if(method_name == "startswith")
              result = obj_sv.value().substr(0, pre_sv.value().size()) ==
                       pre_sv.value();
            else
              result = obj_sv.value().size() >= pre_sv.value().size() &&
                       obj_sv.value().substr(
                         obj_sv.value().size() - pre_sv.value().size()) ==
                         pre_sv.value();
            return result ? exprt{true_exprt{}} : exprt{false_exprt{}};
          }
          if(is_python_string_type(prefix.type()))
          {
            const auto &data_type = array_typet(
              unsignedbv_typet{8},
              from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));
            member_exprt obj_data{obj, "data", data_type};
            member_exprt obj_len{obj, "length", signedbv_typet{64}};
            member_exprt pre_data{prefix, "data", data_type};
            member_exprt pre_len{prefix, "length", signedbv_typet{64}};

            // Build conjunction: all prefix bytes match
            exprt result = binary_relation_exprt{pre_len, ID_le, obj_len};
            for(std::size_t i = 0; i < PYTHON_MAX_STRING_LENGTH; i++)
            {
              exprt idx = from_integer(i, signedbv_typet{64});
              exprt in_prefix = binary_relation_exprt{idx, ID_lt, pre_len};
              exprt obj_idx =
                (method_name == "endswith")
                  ? minus_exprt{minus_exprt{obj_len, pre_len}, from_integer(-static_cast<long long>(i), signedbv_typet{64})}
                  : idx;
              if(method_name == "endswith")
                obj_idx = plus_exprt{minus_exprt{obj_len, pre_len}, idx};
              exprt match = equal_exprt{
                index_exprt{obj_data, obj_idx}, index_exprt{pre_data, idx}};
              result = and_exprt{result, or_exprt{not_exprt{in_prefix}, match}};
            }
            return result;
          }
        }
        // PLib stdtypes: exact string predicates
        if(
          method_name == "isdigit" || method_name == "isalpha" ||
          method_name == "isalnum" || method_name == "isupper" ||
          method_name == "islower" || method_name == "isspace" ||
          method_name == "isascii")
        {
          // Constant-string optimization
          auto sv = extract_string_value(obj);
          if(sv.has_value())
          {
            const std::string &s = sv.value();
            bool result = !s.empty();
            for(char c : s)
            {
              unsigned char uc = static_cast<unsigned char>(c);
              if(
                method_name == "isdigit" || method_name == "isdecimal" ||
                method_name == "isnumeric")
                result = result && std::isdigit(uc);
              else if(method_name == "isalpha")
                result = result && (std::isalpha(uc) || uc >= 0x80);
              else if(method_name == "isalnum")
                result = result && (std::isalnum(uc) || uc >= 0x80);
              else if(method_name == "isupper")
                result = result && (!std::isalpha(uc) || std::isupper(uc));
              else if(method_name == "islower")
                result = result && (!std::isalpha(uc) || std::islower(uc));
              else if(method_name == "isspace")
                result = result && std::isspace(uc);
              else if(method_name == "isascii")
                result = result && (uc < 128);
            }
            if(method_name == "isascii" && s.empty())
              result = true; // empty string is ASCII
            return result ? exprt{true_exprt{}} : exprt{false_exprt{}};
          }
          const auto &data_type = array_typet(
            unsignedbv_typet{8},
            from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));
          // Pointer-based string: return nondet for non-constant
          return side_effect_expr_nondett{
            python_string_type(), source_locationt{}};
          member_exprt data{obj, "data", data_type};
          member_exprt length{obj, "length", signedbv_typet{64}};

          // length > 0 AND for all i < length: char_predicate(data[i])
          exprt result = binary_relation_exprt{
            length, ID_gt, from_integer(0, signedbv_typet{64})};
          for(std::size_t i = 0; i < PYTHON_MAX_STRING_LENGTH; i++)
          {
            exprt idx = from_integer(i, signedbv_typet{64});
            exprt in_range = binary_relation_exprt{idx, ID_lt, length};
            exprt ch = index_exprt{data, idx};
            exprt pred;
            if(
              method_name == "isdigit" || method_name == "isdecimal" ||
              method_name == "isnumeric")
              pred = and_exprt{
                binary_relation_exprt{
                  ch, ID_ge, from_integer('0', unsignedbv_typet{8})},
                binary_relation_exprt{
                  ch, ID_le, from_integer('9', unsignedbv_typet{8})}};
            else if(method_name == "isalpha")
              pred = or_exprt{
                or_exprt{
                  and_exprt{
                    binary_relation_exprt{
                      ch, ID_ge, from_integer('a', unsignedbv_typet{8})},
                    binary_relation_exprt{
                      ch, ID_le, from_integer('z', unsignedbv_typet{8})}},
                  and_exprt{
                    binary_relation_exprt{
                      ch, ID_ge, from_integer('A', unsignedbv_typet{8})},
                    binary_relation_exprt{
                      ch, ID_le, from_integer('Z', unsignedbv_typet{8})}}},
                // UTF-8: first byte >= 0xC0 or continuation byte 0x80-0xBF
                binary_relation_exprt{
                  ch, ID_ge, from_integer(0x80, unsignedbv_typet{8})}};
            else if(method_name == "isalnum")
              pred = or_exprt{
                and_exprt{
                  binary_relation_exprt{
                    ch, ID_ge, from_integer('0', unsignedbv_typet{8})},
                  binary_relation_exprt{
                    ch, ID_le, from_integer('9', unsignedbv_typet{8})}},
                or_exprt{
                  and_exprt{
                    binary_relation_exprt{
                      ch, ID_ge, from_integer('a', unsignedbv_typet{8})},
                    binary_relation_exprt{
                      ch, ID_le, from_integer('z', unsignedbv_typet{8})}},
                  and_exprt{
                    binary_relation_exprt{
                      ch, ID_ge, from_integer('A', unsignedbv_typet{8})},
                    binary_relation_exprt{
                      ch, ID_le, from_integer('Z', unsignedbv_typet{8})}}}};
            else if(method_name == "isupper")
              pred = and_exprt{
                binary_relation_exprt{
                  ch, ID_ge, from_integer('A', unsignedbv_typet{8})},
                binary_relation_exprt{
                  ch, ID_le, from_integer('Z', unsignedbv_typet{8})}};
            else if(method_name == "islower")
              pred = and_exprt{
                binary_relation_exprt{
                  ch, ID_ge, from_integer('a', unsignedbv_typet{8})},
                binary_relation_exprt{
                  ch, ID_le, from_integer('z', unsignedbv_typet{8})}};
            else if(method_name == "isspace")
              pred = or_exprt{
                equal_exprt{ch, from_integer(' ', unsignedbv_typet{8})},
                or_exprt{
                  equal_exprt{ch, from_integer('\t', unsignedbv_typet{8})},
                  equal_exprt{ch, from_integer('\n', unsignedbv_typet{8})}}};
            else // isascii
              pred = binary_relation_exprt{
                ch, ID_le, from_integer(127, unsignedbv_typet{8})};
            result = and_exprt{result, or_exprt{not_exprt{in_range}, pred}};
          }
          return result;
        }
        if(
          method_name == "startswith" || method_name == "endswith" ||
          method_name == "istitle" || method_name == "isidentifier" ||
          method_name == "isprintable")
        {
          // Use solver for startswith/endswith on non-constant strings
          if(
            (method_name == "startswith" || method_name == "endswith") &&
            args.is_array() && !as_array(args).empty())
          {
            exprt prefix = convert_expression(*as_array(args).begin());
            if(is_python_string_type(prefix.type()))
            {
              return emit_string_bool_function(
                method_name == "startswith" ? ID_cprover_string_is_prefix_func
                                            : ID_cprover_string_is_suffix_func,
                prefix,
                obj,
                symbol_table,
                pending_checks);
            }
          }
          return side_effect_expr_nondett{bool_typet{}, get_location(expr)};
        }
        if(
          method_name == "find" || method_name == "index" ||
          method_name == "rfind" || method_name == "rindex" ||
          method_name == "count")
        {
          auto str_val = extract_string_value(obj);
          if(!str_val.has_value() && obj.id() == ID_symbol)
          {
            auto it =
              string_constants.find(to_symbol_expr(obj).get_identifier());
            if(it != string_constants.end())
              str_val = it->second;
          }
          if(str_val.has_value() && args.is_array() && !as_array(args).empty())
          {
            auto ait = as_array(args).begin();
            exprt arg_expr = convert_expression(*ait);
            auto arg_val = extract_string_value(arg_expr);
            if(!arg_val.has_value() && arg_expr.id() == ID_symbol)
            {
              auto it2 = string_constants.find(
                to_symbol_expr(arg_expr).get_identifier());
              if(it2 != string_constants.end())
                arg_val = it2->second;
            }
            // Parse optional start/end range
            int start = 0, end = -1;
            ++ait;
            if(ait != as_array(args).end())
            {
              auto sv2 = try_eval_double(convert_expression(*ait));
              if(sv2.has_value())
                start = static_cast<int>(sv2.value());
              ++ait;
              if(ait != as_array(args).end())
              {
                auto ev = try_eval_double(convert_expression(*ait));
                if(ev.has_value())
                  end = static_cast<int>(ev.value());
              }
            }
            if(arg_val.has_value())
            {
              std::string s = str_val.value();
              int slen = static_cast<int>(s.size());
              if(start < 0)
                start += slen;
              if(start < 0)
                start = 0;
              if(end < 0)
                end = slen;
              if(end > slen)
                end = slen;
              std::string slice =
                (start < end) ? s.substr(start, end - start) : "";
              const std::string &sub = arg_val.value();
              if(method_name == "find")
              {
                auto pos = slice.find(sub);
                return from_integer(
                  pos == std::string::npos
                    ? -1
                    : static_cast<long long>(pos + start),
                  python_int_type());
              }
              if(method_name == "rfind")
              {
                auto pos = slice.rfind(sub);
                return from_integer(
                  pos == std::string::npos
                    ? -1
                    : static_cast<long long>(pos + start),
                  python_int_type());
              }
              if(method_name == "index" || method_name == "rindex")
              {
                auto pos =
                  (method_name == "index") ? s.find(sub) : s.rfind(sub);
                if(pos == std::string::npos)
                {
                  // Raise ValueError
                  const symbolt *exc_sym =
                    symbol_table.lookup("python::__exception_active");
                  if(exc_sym)
                    pending_checks.push_back(code_frontend_assignt{
                      exc_sym->symbol_expr(), true_exprt{}});
                  return from_integer(-1, python_int_type());
                }
                return from_integer(
                  static_cast<long long>(pos), python_int_type());
              }
              if(method_name == "count")
              {
                long long cnt = 0;
                size_t pos = 0;
                while((pos = slice.find(sub, pos)) != std::string::npos)
                {
                  cnt++;
                  pos += sub.empty() ? 1 : sub.size();
                }
                if(sub.empty())
                  cnt = static_cast<long long>(slice.size() + 1);
                return from_integer(cnt, python_int_type());
              }
            }
          }
          // Constrained nondet for symbolic find/count
          {
            static unsigned find_ctr = 0;
            std::string tn = "__find_" + std::to_string(find_ctr++);
            std::string tq = qualify_name(tn);
            irep_idt ti{tq};
            if(symbol_table.lookup(ti) == nullptr)
            {
              symbolt ts{ti, python_int_type(), "python"};
              ts.base_name = tn;
              ts.is_lvalue = true;
              ts.is_state_var = true;
              symbol_table.add(ts);
            }
            symbol_exprt tv = symbol_table.lookup_ref(ti).symbol_expr();
            pending_checks.push_back(code_frontend_assignt{
              tv,
              side_effect_expr_nondett{python_int_type(), get_location(expr)}});
            member_exprt slen{obj, "length", signedbv_typet{64}};
            if(method_name == "count")
              pending_checks.push_back(code_assumet{and_exprt{
                binary_relation_exprt{
                  tv, ID_ge, from_integer(0, python_int_type())},
                binary_relation_exprt{tv, ID_le, slen}}});
            else // find, rfind, index, rindex
              pending_checks.push_back(code_assumet{and_exprt{
                binary_relation_exprt{
                  tv, ID_ge, from_integer(-1, python_int_type())},
                binary_relation_exprt{tv, ID_lt, slen}}});
            return std::move(tv);
          }
        }
        if(method_name == "join")
        {
          // sep.join(lst) — for constant sep and list of constant strings
          if(args.is_array() && !as_array(args).empty())
          {
            exprt list_arg = convert_expression(*as_array(args).begin());
            auto sep_val = extract_string_value(obj);
            if(sep_val.has_value() && is_python_list_type(list_arg.type()))
            {
              // Look up list literal for symbols
              const exprt *list_val = &list_arg;
              if(list_arg.id() == ID_symbol)
              {
                auto it =
                  list_literals.find(to_symbol_expr(list_arg).get_identifier());
                if(it != list_literals.end())
                  list_val = &it->second;
              }
              const auto &list_st = to_struct_type(list_arg.type());
              to_array_type(list_st.components()[1].type());
              if(
                list_val->id() == ID_struct &&
                list_val->operands().size() >= 2 &&
                list_val->operands()[0].is_constant())
              {
                mp_integer len;
                if(!to_integer(to_constant_expr(list_val->operands()[0]), len))
                {
                  std::string result;
                  bool all_const = true;
                  const exprt &data_arr = list_val->operands()[1];
                  for(mp_integer i = 0; i < len; ++i)
                  {
                    auto idx = i.to_ulong();
                    if(idx < data_arr.operands().size())
                    {
                      auto sv = extract_string_value(data_arr.operands()[idx]);
                      if(sv.has_value())
                      {
                        if(i > 0)
                          result += sep_val.value();
                        result += sv.value();
                      }
                      else
                        all_const = false;
                    }
                  }
                  if(all_const)
                    return python_string_literal(result);
                }
              }
            }
          }
          return side_effect_expr_nondett{
            python_string_type(), get_location(expr)};
        }
        if(method_name == "partition" || method_name == "rpartition")
        {
          return side_effect_expr_nondett{
            python_string_type(), get_location(expr)};
        }
      }

      // PLib stdtypes: Dict methods (array-based model)
      if(is_python_dict_type(obj_base_type))
      {
        if(method_name == "get")
        {
          // d.get(key, default) — scan keys array
          if(args.is_array() && !as_array(args).empty())
          {
            auto arg_it = as_array(args).begin();
            exprt key_expr = convert_expression(*arg_it);
            const auto &dict_st = to_struct_type(obj_base_type);
            const auto &keys_type =
              to_array_type(dict_st.components()[1].type());
            const auto &vals_type =
              to_array_type(dict_st.components()[2].type());
            member_exprt length{obj, "length", signedbv_typet{64}};
            member_exprt keys{obj, "keys", keys_type};
            member_exprt vals{obj, "values", vals_type};

            if(key_expr.type() != keys_type.element_type())
              key_expr = safe_typecast(key_expr, keys_type.element_type());

            // Default value: None if not specified, else second arg
            exprt default_val = from_integer(
              mp_integer{-4611686018427387904LL}, vals_type.element_type());
            ++arg_it;
            if(arg_it != as_array(args).end())
              default_val = safe_typecast(
                convert_expression(*arg_it), vals_type.element_type());

            // Constant-key fast path: same logic as subscript read.
            // When the dict value is a literal struct or a tracked
            // dict_literal symbol, and the key is a string constant,
            // resolve at conversion time.
            auto key_str = extract_string_value(key_expr);
            if(key_str.has_value())
            {
              const exprt *dict_val = nullptr;
              if(obj.id() == ID_struct)
                dict_val = &obj;
              else if(obj.id() == ID_symbol)
              {
                auto it =
                  dict_literals.find(to_symbol_expr(obj).get_identifier());
                if(it != dict_literals.end())
                  dict_val = &it->second;
              }
              if(
                dict_val != nullptr && dict_val->operands().size() >= 3 &&
                dict_val->operands()[0].is_constant())
              {
                mp_integer len_val;
                if(!to_integer(
                     to_constant_expr(dict_val->operands()[0]), len_val))
                {
                  const exprt &keys_arr = dict_val->operands()[1];
                  const exprt &vals_arr = dict_val->operands()[2];
                  for(mp_integer i = 0; i < len_val; ++i)
                  {
                    auto idx = i.to_ulong();
                    if(idx < keys_arr.operands().size())
                    {
                      auto kv = extract_string_value(keys_arr.operands()[idx]);
                      if(kv.has_value() && kv.value() == key_str.value())
                        return vals_arr.operands()[idx];
                    }
                  }
                  // Key not present in the literal: default path.
                  return default_val;
                }
              }
            }

            exprt result = default_val;
            for(int i = PYTHON_MAX_DICT_SIZE - 1; i >= 0; i--)
            {
              exprt idx = from_integer(i, signedbv_typet{64});
              exprt in_range = binary_relation_exprt{idx, ID_lt, length};
              exprt match = equal_exprt{index_exprt{keys, idx}, key_expr};
              result = if_exprt{
                and_exprt{in_range, match}, index_exprt{vals, idx}, result};
            }
            return result;
          }
          return side_effect_expr_nondett{
            python_int_type(), get_location(expr)};
        }
        if(method_name == "keys")
        {
          // Constant fast path: when obj is a literal struct
          // or a tracked dict_literal symbol, build the keys
          // list directly from the stored operands — no
          // member_exprt indirection.
          const exprt *dict_val = nullptr;
          if(obj.id() == ID_struct)
            dict_val = &obj;
          else if(obj.id() == ID_symbol)
          {
            auto it = dict_literals.find(to_symbol_expr(obj).get_identifier());
            if(it != dict_literals.end())
              dict_val = &it->second;
          }
          const auto &dict_st = to_struct_type(obj_base_type);
          const auto &keys_type = to_array_type(dict_st.components()[1].type());
          typet key_type = keys_type.element_type();
          struct_typet list_type = python_list_type(key_type);
          const auto &list_data_type =
            to_array_type(list_type.components()[1].type());
          if(
            dict_val != nullptr && dict_val->operands().size() >= 3 &&
            dict_val->operands()[0].is_constant())
          {
            exprt::operandst elems;
            for(const auto &k : dict_val->operands()[1].operands())
              elems.push_back(k);
            while(elems.size() < PYTHON_MAX_LIST_LENGTH)
              elems.push_back(safe_zero(key_type));
            return struct_exprt{
              {dict_val->operands()[0],
               array_exprt{std::move(elems), list_data_type}},
              list_type};
          }
          // d.keys() → list of d.keys[0..d.length-1]
          member_exprt length{obj, "length", signedbv_typet{64}};
          member_exprt keys{obj, "keys", keys_type};
          exprt::operandst elems;
          for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
            elems.push_back(
              index_exprt{keys, from_integer(i, signedbv_typet{64})});
          while(elems.size() < PYTHON_MAX_LIST_LENGTH)
            elems.push_back(safe_zero(key_type));
          return struct_exprt{
            {length, array_exprt{std::move(elems), list_data_type}}, list_type};
        }
        if(method_name == "values")
        {
          const exprt *dict_val = nullptr;
          if(obj.id() == ID_struct)
            dict_val = &obj;
          else if(obj.id() == ID_symbol)
          {
            auto it = dict_literals.find(to_symbol_expr(obj).get_identifier());
            if(it != dict_literals.end())
              dict_val = &it->second;
          }
          const auto &dict_st = to_struct_type(obj_base_type);
          const auto &vals_type = to_array_type(dict_st.components()[2].type());
          typet val_type = vals_type.element_type();
          struct_typet list_type = python_list_type(val_type);
          const auto &list_data_type =
            to_array_type(list_type.components()[1].type());
          if(
            dict_val != nullptr && dict_val->operands().size() >= 3 &&
            dict_val->operands()[0].is_constant())
          {
            exprt::operandst elems;
            for(const auto &v : dict_val->operands()[2].operands())
              elems.push_back(v);
            while(elems.size() < PYTHON_MAX_LIST_LENGTH)
              elems.push_back(safe_zero(val_type));
            return struct_exprt{
              {dict_val->operands()[0],
               array_exprt{std::move(elems), list_data_type}},
              list_type};
          }
          member_exprt length{obj, "length", signedbv_typet{64}};
          member_exprt vals{obj, "values", vals_type};
          exprt::operandst elems;
          for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
            elems.push_back(
              index_exprt{vals, from_integer(i, signedbv_typet{64})});
          while(elems.size() < PYTHON_MAX_LIST_LENGTH)
            elems.push_back(safe_zero(val_type));
          return struct_exprt{
            {length, array_exprt{std::move(elems), list_data_type}}, list_type};
        }
        if(method_name == "items")
        {
          // PLR dict.items(): view of (key, value) pairs. We
          // materialise as a python_list of python_tuple(key,
          // value) — precise for dict literals, falls through
          // to nondet otherwise.
          const exprt *dict_val = nullptr;
          if(obj.id() == ID_struct)
            dict_val = &obj;
          else if(obj.id() == ID_symbol)
          {
            auto it = dict_literals.find(to_symbol_expr(obj).get_identifier());
            if(it != dict_literals.end())
              dict_val = &it->second;
          }
          if(
            dict_val != nullptr && dict_val->operands().size() >= 3 &&
            dict_val->operands()[0].is_constant())
          {
            mp_integer lv;
            if(!to_integer(to_constant_expr(dict_val->operands()[0]), lv))
            {
              const auto &dict_st = to_struct_type(obj_base_type);
              const auto &keys_type =
                to_array_type(dict_st.components()[1].type());
              const auto &vals_type =
                to_array_type(dict_st.components()[2].type());
              const typet key_t = keys_type.element_type();
              const typet val_t = vals_type.element_type();
              struct_typet tuple_t = python_tuple_type({key_t, val_t});
              tuple_t.set_tag("python_tuple");
              struct_typet list_t = python_list_type(tuple_t);
              const auto &list_data_type =
                to_array_type(list_t.components()[1].type());
              const exprt &src_keys = dict_val->operands()[1];
              const exprt &src_vals = dict_val->operands()[2];
              exprt::operandst elems;
              for(mp_integer i = 0; i < lv; ++i)
              {
                auto idx = i.to_ulong();
                if(
                  idx >= src_keys.operands().size() ||
                  idx >= src_vals.operands().size())
                  break;
                elems.push_back(struct_exprt{
                  {src_keys.operands()[idx], src_vals.operands()[idx]},
                  tuple_t});
              }
              while(elems.size() < PYTHON_MAX_LIST_LENGTH)
                elems.push_back(safe_zero(tuple_t));
              return struct_exprt{
                {dict_val->operands()[0],
                 array_exprt{std::move(elems), list_data_type}},
                list_t};
            }
          }
          // Returns list of tuples — simplified to nondet for now
          return side_effect_expr_nondett{
            python_list_type(python_int_type()), get_location(expr)};
        }
        if(method_name == "clear")
        {
          // d.clear() → set d.length = 0
          if(obj.id() == ID_symbol)
          {
            member_exprt length{obj, "length", signedbv_typet{64}};
            pending_checks.push_back(code_frontend_assignt{
              length, from_integer(0, signedbv_typet{64})});
          }
          return from_integer(0, python_int_type()); // returns None
        }
        if(method_name == "copy")
        {
          return obj; // shallow copy = same struct
        }
        if(method_name == "update")
        {
          // PLR dict.update(other): merge entries from other
          // into d. If other is a dict literal (struct_exprt
          // with constant length + keys + values), emit one
          // dict-subscript-assign statement per entry to
          // preserve dict_literals tracking on both sides.
          // Non-literal 'other' or non-dict — fall through to
          // returning None (no mutation) as an
          // over-approximation.
          if(args.is_array() && !as_array(args).empty())
          {
            exprt other = convert_expression(*as_array(args).begin());
            const exprt *olit = nullptr;
            if(other.id() == ID_struct)
              olit = &other;
            else if(other.id() == ID_symbol)
            {
              auto it =
                dict_literals.find(to_symbol_expr(other).get_identifier());
              if(it != dict_literals.end())
                olit = &it->second;
            }
            if(
              olit != nullptr && olit->operands().size() >= 3 &&
              olit->operands()[0].is_constant() &&
              is_python_dict_type(obj.type()))
            {
              mp_integer olen;
              if(!to_integer(to_constant_expr(olit->operands()[0]), olen))
              {
                // Invalidate dict_literals tracking on obj so
                // subsequent d["key"] lookups read from the
                // mutated struct, not the stale literal.
                if(obj.id() == ID_symbol)
                  dict_literals.erase(to_symbol_expr(obj).get_identifier());
                const auto &dst_st = to_struct_type(obj.type());
                const auto &keys_type =
                  to_array_type(dst_st.components()[1].type());
                const auto &vals_type =
                  to_array_type(dst_st.components()[2].type());
                member_exprt dst_len{obj, "length", signedbv_typet{64}};
                member_exprt dst_keys{obj, "keys", keys_type};
                member_exprt dst_vals{obj, "values", vals_type};
                const exprt &src_keys = olit->operands()[1];
                const exprt &src_vals = olit->operands()[2];
                for(mp_integer i = 0; i < olen; ++i)
                {
                  auto idx = i.to_ulong();
                  if(
                    idx >= src_keys.operands().size() ||
                    idx >= src_vals.operands().size())
                    break;
                  exprt k = src_keys.operands()[idx];
                  exprt v = src_vals.operands()[idx];
                  if(k.type() != keys_type.element_type())
                    k = safe_typecast(k, keys_type.element_type());
                  if(v.type() != vals_type.element_type())
                    v = safe_typecast(v, vals_type.element_type());
                  // Scan existing keys, replace or append.
                  // Matches the Assign-to-subscript handler
                  // pattern used for d[k] = v statements.
                  static unsigned upd_ctr = 0;
                  std::string fn = "__upd_found_" + std::to_string(upd_ctr++);
                  std::string fq = qualify_name(fn);
                  irep_idt fi{fq};
                  if(symbol_table.lookup(fi) == nullptr)
                  {
                    symbolt fs{fi, bool_typet{}, "python"};
                    fs.base_name = fn;
                    fs.is_lvalue = true;
                    fs.is_state_var = true;
                    symbol_table.add(fs);
                  }
                  symbol_exprt found =
                    symbol_table.lookup_ref(fi).symbol_expr();
                  pending_checks.push_back(
                    code_frontend_assignt{found, false_exprt{}});
                  for(std::size_t si = 0; si < PYTHON_MAX_DICT_SIZE; si++)
                  {
                    exprt sidx = from_integer(si, signedbv_typet{64});
                    exprt in_range =
                      binary_relation_exprt{sidx, ID_lt, dst_len};
                    exprt match = equal_exprt{index_exprt{dst_keys, sidx}, k};
                    code_blockt upd;
                    upd.add(
                      code_frontend_assignt{index_exprt{dst_vals, sidx}, v});
                    upd.add(code_frontend_assignt{found, true_exprt{}});
                    pending_checks.push_back(code_ifthenelset{
                      and_exprt{in_range, match}, std::move(upd)});
                  }
                  code_blockt append;
                  append.add(
                    code_frontend_assignt{index_exprt{dst_keys, dst_len}, k});
                  append.add(
                    code_frontend_assignt{index_exprt{dst_vals, dst_len}, v});
                  append.add(code_frontend_assignt{
                    dst_len,
                    plus_exprt{dst_len, from_integer(1, signedbv_typet{64})}});
                  pending_checks.push_back(
                    code_ifthenelset{not_exprt{found}, std::move(append)});
                }
              }
            }
          }
          return from_integer(0, python_int_type());
        }
        if(
          method_name == "setdefault" || method_name == "pop" ||
          method_name == "popitem")
          return side_effect_expr_nondett{
            python_int_type(), get_location(expr)};
      }

      // Python set methods: operate on the 64-bit bitmap so
      // they remain precise for int elements in [0, 64).
      // Falls through to nondet for element values outside
      // that range or for set-typed structs whose elements
      // are not int.
      if(is_python_set_type(obj_base_type))
      {
        member_exprt bm{obj, "bitmap", unsignedbv_typet{64}};
        member_exprt off{obj, "offset", signedbv_typet{64}};

        auto arg_bitmap = [&](const exprt &s) {
          return member_exprt{s, "bitmap", unsignedbv_typet{64}};
        };

        // Read a single int arg; cast to unsigned{64} for shifts.
        auto single_arg_bit_shift = [&]() -> std::optional<exprt>
        {
          if(!args.is_array() || as_array(args).empty())
            return std::nullopt;
          exprt val = convert_expression(*as_array(args).begin());
          if(val.type() != signedbv_typet{64})
            val = safe_typecast(val, signedbv_typet{64});
          exprt shamt = typecast_exprt{val, unsignedbv_typet{64}};
          return shamt;
        };

        if(method_name == "add" || method_name == "discard")
        {
          auto shamt = single_arg_bit_shift();
          if(!shamt.has_value())
            return side_effect_expr_nondett{bool_typet{}, get_location(expr)};
          exprt bit = shl_exprt{from_integer(1, unsignedbv_typet{64}), *shamt};
          exprt new_bm = method_name == "add"
                           ? exprt{bitor_exprt{bm, bit}}
                           : exprt{bitand_exprt{bm, bitnot_exprt{bit}}};
          pending_checks.push_back(code_frontend_assignt{bm, new_bm});
          return side_effect_expr_nondett{
            python_int_type(), get_location(expr)};
        }
        if(method_name == "remove")
        {
          // Like discard, but raise KeyError if the element is
          // not present.
          auto shamt = single_arg_bit_shift();
          if(!shamt.has_value())
            return side_effect_expr_nondett{bool_typet{}, get_location(expr)};
          exprt bit = shl_exprt{from_integer(1, unsignedbv_typet{64}), *shamt};
          exprt present = notequal_exprt{
            bitand_exprt{bm, bit}, from_integer(0, unsignedbv_typet{64})};
          add_check(
            present,
            "exception",
            "KeyError: element not in set",
            get_location(expr));
          exprt new_bm = bitand_exprt{bm, bitnot_exprt{bit}};
          pending_checks.push_back(code_frontend_assignt{bm, new_bm});
          return side_effect_expr_nondett{
            python_int_type(), get_location(expr)};
        }
        if(
          method_name == "union" || method_name == "intersection" ||
          method_name == "difference" || method_name == "symmetric_difference")
        {
          if(!args.is_array() || as_array(args).empty())
            return obj;
          exprt other = convert_expression(*as_array(args).begin());
          if(!is_python_set_type(other.type()))
            return side_effect_expr_nondett{
              python_set_type(), get_location(expr)};
          exprt rbm = arg_bitmap(other);
          exprt new_bm;
          if(method_name == "union")
            new_bm = bitor_exprt{bm, rbm};
          else if(method_name == "intersection")
            new_bm = bitand_exprt{bm, rbm};
          else if(method_name == "difference")
            new_bm = bitand_exprt{bm, bitnot_exprt{rbm}};
          else // symmetric_difference
            new_bm = bitxor_exprt{bm, rbm};
          return struct_exprt{{std::move(new_bm), off}, python_set_type()};
        }
        if(
          method_name == "issubset" || method_name == "issuperset" ||
          method_name == "isdisjoint")
        {
          if(!args.is_array() || as_array(args).empty())
            return true_exprt{};
          exprt other = convert_expression(*as_array(args).begin());
          if(!is_python_set_type(other.type()))
            return side_effect_expr_nondett{bool_typet{}, get_location(expr)};
          exprt rbm = arg_bitmap(other);
          exprt zero = from_integer(0, unsignedbv_typet{64});
          if(method_name == "issubset")
            // A⊆B iff A & ~B == 0
            return equal_exprt{bitand_exprt{bm, bitnot_exprt{rbm}}, zero};
          if(method_name == "issuperset")
            return equal_exprt{bitand_exprt{rbm, bitnot_exprt{bm}}, zero};
          // isdisjoint: A & B == 0
          return equal_exprt{bitand_exprt{bm, rbm}, zero};
        }
        if(method_name == "copy")
          return obj;
        if(method_name == "clear")
        {
          pending_checks.push_back(
            code_frontend_assignt{bm, from_integer(0, unsignedbv_typet{64})});
          return side_effect_expr_nondett{
            python_int_type(), get_location(expr)};
        }
        if(method_name == "pop")
        {
          // Return nondet int and clear a nondet bit — over-approximation
          // that reflects pop removing an arbitrary element.
          pending_checks.push_back(code_frontend_assignt{
            bm,
            side_effect_expr_nondett{
              unsignedbv_typet{64}, get_location(expr)}});
          return side_effect_expr_nondett{
            signedbv_typet{64}, get_location(expr)};
        }
      }

      // PLib stdtypes: List methods (append, sort, reverse, pop, etc.)
      if(is_python_list_type(obj_base_type))
      {
        const auto &list_st = to_struct_type(obj_base_type);
        const auto &data_type = to_array_type(list_st.components()[1].type());
        member_exprt length{obj, "length", signedbv_typet{64}};
        member_exprt data{obj, "data", data_type};

        if(method_name == "reverse")
        {
          // Reverse in place: swap data[i] with data[len-1-i]
          code_blockt block;
          for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH / 2; i++)
          {
            exprt idx = from_integer(i, signedbv_typet{64});
            exprt mirror = minus_exprt{
              minus_exprt{length, from_integer(1, signedbv_typet{64})}, idx};
            exprt cond = binary_relation_exprt{idx, ID_lt, mirror};
            // Swap via temp
            static unsigned rev_counter = 0;
            std::string tmp_name = "__rev_tmp_" + std::to_string(rev_counter++);
            std::string tmp_qname = qualify_name(tmp_name);
            irep_idt tmp_id{tmp_qname};
            if(symbol_table.lookup(tmp_id) == nullptr)
            {
              symbolt tmp_sym{tmp_id, data_type.element_type(), "python"};
              tmp_sym.base_name = tmp_name;
              tmp_sym.is_lvalue = true;
              tmp_sym.is_state_var = true;
              symbol_table.add(tmp_sym);
            }
            symbol_exprt tmp = symbol_table.lookup_ref(tmp_id).symbol_expr();
            code_blockt swap;
            swap.add(code_frontend_assignt{tmp, index_exprt{data, idx}});
            swap.add(code_frontend_assignt{
              index_exprt{data, idx}, index_exprt{data, mirror}});
            swap.add(code_frontend_assignt{index_exprt{data, mirror}, tmp});
            pending_checks.push_back(code_ifthenelset{cond, std::move(swap)});
          }
          return from_integer(0, python_int_type()); // None
        }

        if(method_name == "sort")
        {
          // Bubble sort via pending_checks (correct for bounded lists)
          for(std::size_t pass = 0; pass < PYTHON_MAX_LIST_LENGTH; pass++)
          {
            for(std::size_t i = 0; i + 1 < PYTHON_MAX_LIST_LENGTH; i++)
            {
              exprt idx = from_integer(i, signedbv_typet{64});
              exprt next = from_integer(i + 1, signedbv_typet{64});
              exprt in_bounds = binary_relation_exprt{next, ID_lt, length};
              exprt should_swap = binary_relation_exprt{
                index_exprt{data, idx}, ID_gt, index_exprt{data, next}};
              // Conditional swap
              static unsigned sort_counter = 0;
              std::string tmp_name =
                "__sort_tmp_" + std::to_string(sort_counter++);
              std::string tmp_qname = qualify_name(tmp_name);
              irep_idt tmp_id{tmp_qname};
              if(symbol_table.lookup(tmp_id) == nullptr)
              {
                symbolt tmp_sym{tmp_id, data_type.element_type(), "python"};
                tmp_sym.base_name = tmp_name;
                tmp_sym.is_lvalue = true;
                tmp_sym.is_state_var = true;
                symbol_table.add(tmp_sym);
              }
              symbol_exprt tmp = symbol_table.lookup_ref(tmp_id).symbol_expr();
              code_blockt swap;
              swap.add(code_frontend_assignt{tmp, index_exprt{data, idx}});
              swap.add(code_frontend_assignt{
                index_exprt{data, idx}, index_exprt{data, next}});
              swap.add(code_frontend_assignt{index_exprt{data, next}, tmp});
              pending_checks.push_back(code_ifthenelset{
                and_exprt{in_bounds, should_swap}, std::move(swap)});
            }
          }
          return from_integer(0, python_int_type()); // None
        }

        if(method_name == "pop")
        {
          // PLib stdtypes: pop(i) or pop() — remove and return element
          exprt pop_idx;
          if(args.is_array() && !as_array(args).empty())
            pop_idx = safe_typecast(
              convert_expression(*as_array(args).begin()), signedbv_typet{64});
          else
            pop_idx = minus_exprt{length, from_integer(1, signedbv_typet{64})};

          static unsigned pop_counter = 0;
          std::string tmp_name = "__pop_tmp_" + std::to_string(pop_counter++);
          std::string tmp_qname = qualify_name(tmp_name);
          irep_idt tmp_id{tmp_qname};
          if(symbol_table.lookup(tmp_id) == nullptr)
          {
            symbolt tmp_sym{tmp_id, data_type.element_type(), "python"};
            tmp_sym.base_name = tmp_name;
            tmp_sym.is_lvalue = true;
            tmp_sym.is_state_var = true;
            symbol_table.add(tmp_sym);
          }
          symbol_exprt tmp = symbol_table.lookup_ref(tmp_id).symbol_expr();
          // Save element at index
          pending_checks.push_back(
            code_frontend_assignt{tmp, index_exprt{data, pop_idx}});
          // Shift elements left from pop_idx
          for(std::size_t j = 0; j + 1 < PYTHON_MAX_LIST_LENGTH; j++)
          {
            exprt jexpr = from_integer(j, signedbv_typet{64});
            exprt guard = and_exprt{
              binary_relation_exprt{jexpr, ID_ge, pop_idx},
              binary_relation_exprt{
                jexpr,
                ID_lt,
                minus_exprt{length, from_integer(1, signedbv_typet{64})}}};
            pending_checks.push_back(code_ifthenelset{
              guard,
              code_frontend_assignt{
                index_exprt{data, jexpr},
                index_exprt{
                  data,
                  plus_exprt{jexpr, from_integer(1, signedbv_typet{64})}}}});
          }
          // Decrement length
          pending_checks.push_back(code_frontend_assignt{
            member_exprt{obj, "length", signedbv_typet{64}},
            minus_exprt{length, from_integer(1, signedbv_typet{64})}});
          return std::move(tmp);
        }

        // PLib stdtypes: list.index(value) — return index of first occurrence
        if(method_name == "index")
        {
          if(args.is_array() && !as_array(args).empty())
          {
            exprt search = convert_expression(*as_array(args).begin());
            if(search.type() != data_type.element_type())
              search = safe_typecast(search, data_type.element_type());
            // Build if-then-else chain: check from end to start
            exprt result = from_integer(-1, python_int_type()); // not found
            for(int i = PYTHON_MAX_LIST_LENGTH - 1; i >= 0; i--)
            {
              exprt idx = from_integer(i, signedbv_typet{64});
              exprt in_range = binary_relation_exprt{idx, ID_lt, length};
              exprt match = equal_exprt{index_exprt{data, idx}, search};
              result = if_exprt{and_exprt{in_range, match}, idx, result};
            }
            return result;
          }
        }

        // PLib stdtypes: list.extend(iterable)
        if(method_name == "extend")
        {
          if(args.is_array() && !as_array(args).empty())
          {
            exprt arg = convert_expression(*as_array(args).begin());
            if(is_python_list_type(arg.type()))
            {
              member_exprt arg_len{arg, "length", signedbv_typet{64}};
              const auto &arg_data_type = to_array_type(
                to_struct_type(arg.type()).components()[1].type());
              member_exprt arg_data{arg, "data", arg_data_type};
              // Copy elements: obj.data[obj.length + i] = arg.data[i]
              for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
              {
                exprt idx = from_integer(i, signedbv_typet{64});
                exprt dst = plus_exprt{length, idx};
                pending_checks.push_back(code_ifthenelset{
                  binary_relation_exprt{idx, ID_lt, arg_len},
                  code_frontend_assignt{
                    index_exprt{data, dst}, index_exprt{arg_data, idx}}});
              }
              pending_checks.push_back(code_frontend_assignt{
                member_exprt{obj, "length", signedbv_typet{64}},
                plus_exprt{length, arg_len}});
            }
            else if(is_python_string_type(arg.type()))
            {
              // extend with string: iterate characters
              auto sv = extract_string_value(arg);
              if(!sv.has_value() && arg.id() == ID_symbol)
              {
                auto it =
                  string_constants.find(to_symbol_expr(arg).get_identifier());
                if(it != string_constants.end())
                  sv = it->second;
              }
              if(sv.has_value())
              {
                // Constant string: add each char as a single-char string
                for(std::size_t i = 0; i < sv.value().size(); i++)
                {
                  exprt dst =
                    plus_exprt{length, from_integer(i, signedbv_typet{64})};
                  exprt ch_str =
                    python_string_literal(std::string(1, sv.value()[i]));
                  if(ch_str.type() != data_type.element_type())
                    ch_str = safe_typecast(ch_str, data_type.element_type());
                  pending_checks.push_back(
                    code_frontend_assignt{index_exprt{data, dst}, ch_str});
                }
                pending_checks.push_back(code_frontend_assignt{
                  member_exprt{obj, "length", signedbv_typet{64}},
                  plus_exprt{
                    length,
                    from_integer(
                      static_cast<long long>(sv.value().size()),
                      signedbv_typet{64})}});
              }
            }
          }
          return from_integer(0, python_int_type());
        }

        // PLib stdtypes: list.remove(value)
        if(method_name == "remove")
        {
          if(args.is_array() && !as_array(args).empty())
          {
            exprt val = convert_expression(*as_array(args).begin());
            if(val.type() != data_type.element_type())
              val = safe_typecast(val, data_type.element_type());
            // Find first occurrence and shift left
            // Use a found flag to track if we've found the element
            static unsigned rm_counter = 0;
            std::string flag_name =
              "__rm_found_" + std::to_string(rm_counter++);
            std::string flag_qname = qualify_name(flag_name);
            irep_idt flag_id{flag_qname};
            if(symbol_table.lookup(flag_id) == nullptr)
            {
              symbolt flag_sym{flag_id, bool_typet{}, "python"};
              flag_sym.base_name = flag_name;
              flag_sym.is_lvalue = true;
              flag_sym.is_state_var = true;
              symbol_table.add(flag_sym);
            }
            symbol_exprt found = symbol_table.lookup_ref(flag_id).symbol_expr();
            pending_checks.push_back(
              code_frontend_assignt{found, false_exprt{}});
            for(std::size_t i = 0; i + 1 < PYTHON_MAX_LIST_LENGTH; i++)
            {
              exprt idx = from_integer(i, signedbv_typet{64});
              exprt next = from_integer(i + 1, signedbv_typet{64});
              exprt in_bounds = binary_relation_exprt{idx, ID_lt, length};
              exprt is_match = equal_exprt{index_exprt{data, idx}, val};
              // If not found yet and matches, set found
              code_blockt on_match;
              on_match.add(code_frontend_assignt{found, true_exprt{}});
              pending_checks.push_back(code_ifthenelset{
                and_exprt{in_bounds, and_exprt{not_exprt{found}, is_match}},
                std::move(on_match)});
              // If found, shift left
              pending_checks.push_back(code_ifthenelset{
                and_exprt{in_bounds, found},
                code_frontend_assignt{
                  index_exprt{data, idx}, index_exprt{data, next}}});
            }
            pending_checks.push_back(code_ifthenelset{
              found,
              code_frontend_assignt{
                member_exprt{obj, "length", signedbv_typet{64}},
                minus_exprt{length, from_integer(1, signedbv_typet{64})}}});
          }
          return from_integer(0, python_int_type());
        }

        // PLib stdtypes: list.copy()
        if(method_name == "copy")
          return obj; // struct copy
      }

      if(obj_base_type.id() != ID_struct)
      {
        // Tagged-union (python_value_type) values: route the
        // method call through __class_ptr with virtual
        // dispatch on __class_tag.
        //
        // PLR §3.3.2 Method resolution order: Python picks the
        // override along the MRO. Our CLASS tag stores the
        // concrete class tag at wrap time; we emit an if-chain
        // comparing *(__class_ptr as int32*) against the tag
        // of every class that both (a) defines method_name and
        // (b) is a compatible entry point (defines or inherits
        // it). The first matching class wins when multiple
        // classes share the method name but aren't
        // subclass-related — matches Python semantics when the
        // caller has not narrowed via isinstance.
        if(
          obj_base_type.id() == ID_struct_tag &&
          id2string(to_struct_tag_type(obj_base_type).get_identifier()) ==
            std::string{PYTHON_VALUE_TAG})
        {
          // Collect classes that define method_name directly.
          std::vector<std::string> method_owners;
          for(const auto &[cls_name, cls_type] : class_types)
          {
            irep_idt mid{"python::" + cls_name + "::" + method_name};
            const symbolt *msym = symbol_table.lookup(mid);
            if(msym != nullptr && msym->type.id() == ID_code)
              method_owners.push_back(cls_name);
          }
          if(method_owners.empty())
            return side_effect_expr_nondett{obj.type(), get_location(expr)};

          // Build argument list (shared across all branches).
          exprt::operandst call_args;
          if(args.is_array())
          {
            for(const auto &a : as_array(args))
              call_args.push_back(convert_expression(a));
          }

          // Single owner: simple dispatch (common case).
          if(method_owners.size() == 1)
          {
            const std::string &cls_name = method_owners.front();
            const auto &cls_type = class_types.at(cls_name);
            irep_idt mid{"python::" + cls_name + "::" + method_name};
            const symbolt *msym = symbol_table.lookup_ref(mid).name.empty()
                                    ? nullptr
                                    : &symbol_table.lookup_ref(mid);
            const code_typet &mty = to_code_type(msym->type);
            exprt::operandst mcall_args;
            pointer_typet cls_ptr_type{cls_type, 64};
            exprt self_ptr =
              typecast_exprt{python_value_class_ptr(obj), cls_ptr_type};
            bool has_self = !mty.parameters().empty() &&
                            mty.parameters()[0].type().id() == ID_pointer;
            if(has_self)
              mcall_args.push_back(self_ptr);
            for(std::size_t i = 0; i < call_args.size(); i++)
            {
              exprt av = call_args[i];
              std::size_t pidx = mcall_args.size();
              if(
                pidx < mty.parameters().size() &&
                av.type() != mty.parameters()[pidx].type())
                av = safe_typecast(av, mty.parameters()[pidx].type());
              mcall_args.push_back(std::move(av));
            }
            return side_effect_expr_function_callt{
              msym->symbol_expr(),
              std::move(mcall_args),
              mty.return_type(),
              get_location(expr)};
          }

          // Multi-owner: virtual dispatch via __class_tag.
          // Assign the matching branch's call result to a
          // shared tmp symbol.
          //
          // Return type: use the first owner's return type;
          // the code_typet contract across overrides is
          // conventionally the same.
          irep_idt first_mid{
            "python::" + method_owners.front() + "::" + method_name};
          const symbolt *first_sym = symbol_table.lookup(first_mid);
          typet ret_type = to_code_type(first_sym->type).return_type();

          static unsigned vdisp_ctr = 0;
          std::string tn = "__vdisp_" + std::to_string(vdisp_ctr++);
          std::string tq = qualify_name(tn);
          irep_idt ti{tq};
          if(symbol_table.lookup(ti) == nullptr)
          {
            symbolt ts{ti, ret_type, "python"};
            ts.base_name = tn;
            ts.is_lvalue = true;
            ts.is_state_var = true;
            symbol_table.add(ts);
          }
          symbol_exprt result_sym = symbol_table.lookup_ref(ti).symbol_expr();

          // Read __class_tag from the wrapped struct.
          pointer_typet i32_ptr{signedbv_typet{32}, 64};
          dereference_exprt class_tag{
            typecast_exprt{python_value_class_ptr(obj), i32_ptr},
            signedbv_typet{32}};

          for(const auto &cls_name : method_owners)
          {
            const auto &cls_type = class_types.at(cls_name);
            irep_idt mid{"python::" + cls_name + "::" + method_name};
            const symbolt *msym = symbol_table.lookup(mid);
            const code_typet &mty = to_code_type(msym->type);

            exprt::operandst mcall_args;
            pointer_typet cls_ptr_type{cls_type, 64};
            exprt self_ptr =
              typecast_exprt{python_value_class_ptr(obj), cls_ptr_type};
            bool has_self = !mty.parameters().empty() &&
                            mty.parameters()[0].type().id() == ID_pointer;
            if(has_self)
              mcall_args.push_back(self_ptr);
            for(std::size_t i = 0; i < call_args.size(); i++)
            {
              exprt av = call_args[i];
              std::size_t pidx = mcall_args.size();
              if(
                pidx < mty.parameters().size() &&
                av.type() != mty.parameters()[pidx].type())
                av = safe_typecast(av, mty.parameters()[pidx].type());
              mcall_args.push_back(std::move(av));
            }

            auto tit = class_tag_ids.find(cls_name);
            if(tit == class_tag_ids.end())
              continue;
            exprt tag_match = equal_exprt{
              class_tag, from_integer(tit->second, signedbv_typet{32})};

            code_blockt branch;
            side_effect_expr_function_callt call{
              msym->symbol_expr(),
              std::move(mcall_args),
              mty.return_type(),
              get_location(expr)};
            exprt call_result = call;
            if(call_result.type() != ret_type)
              call_result = safe_typecast(call_result, ret_type);
            branch.add(code_frontend_assignt{result_sym, call_result});
            pending_checks.push_back(
              code_ifthenelset{std::move(tag_match), std::move(branch)});
          }
          return std::move(result_sym);
        }
        // Non-struct type (e.g., struct_tag_typet for strings) — return nondet
        return side_effect_expr_nondett{obj.type(), get_location(expr)};
      }
      if(obj_base_type.id() != ID_struct)
      {
        // struct_tag: resolve to the underlying struct type
        // and fall through to the unified struct path below.
        // This replaces an earlier dual-path duplication where
        // struct_tag objects only got missing-method detection
        // and not actual method dispatch.
        if(obj_base_type.id() == ID_struct_tag)
        {
          const auto &tagged = to_struct_tag_type(obj_base_type);
          const symbolt *type_sym =
            symbol_table.lookup(tagged.get_identifier());
          if(type_sym != nullptr && type_sym->is_type)
            obj_base_type = type_sym->type;
        }
      }
      if(obj_base_type.id() != ID_struct)
      {
        return side_effect_expr_nondett{obj.type(), get_location(expr)};
      }
      const auto &st = to_struct_type(obj_base_type);
      std::string tag = id2string(st.get_tag());
      // tag is "python_class_ClassName" OR "ClassName" for
      // PySpec-style imported stub classes.
      {
        std::string class_name =
          (tag.substr(0, 13) == "python_class_") ? tag.substr(13) : tag;
        if(class_types.count(class_name) > 0)
        {
          irep_idt method_id{"python::" + class_name + "::" + method_name};
          const symbolt *method_sym = symbol_table.lookup(method_id);
          // Strict missing-method detection: if the class is in
          // class_types (we know its structure) but the method
          // isn't declared, raise AttributeError rather than
          // silently over-approximating.
          if(
            method_sym == nullptr && method_name.substr(0, 2) != "__" &&
            !python_lazy_stubs)
          {
            // boto3 BaseClient inherited methods — these aren't
            // declared in service-specific stubs but every
            // boto3.client(...) instance has them. Suppress the
            // missing-method check to avoid stub-completeness FPs.
            static const std::set<std::string> boto3_base_methods{
              "get_paginator",
              "can_paginate",
              "get_waiter",
              "meta",
              "exceptions",
              "close",
              "generate_presigned_url",
              "generate_presigned_post"};
            const bool is_boto3_base =
              boto3_base_methods.count(method_name) > 0;
            // Forward-reference suppression: when a method calls
            // another method on the same class (or a known
            // sibling/base class), the callee may not yet have
            // been registered in the symbol table at conversion
            // time. We collected the full set of declared names
            // in class_declared_methods during convert_class_def's
            // pre-pass, so consult that before flagging.
            bool declared_on_class = false;
            {
              auto cdm = class_declared_methods.find(class_name);
              if(
                cdm != class_declared_methods.end() &&
                cdm->second.count(method_name) > 0)
                declared_on_class = true;
            }
            irep_idt exc_id{"python::__exception_active"};
            // For genuinely missing methods (not boto3-base
            // inherited and not forward-referenced), set
            // __exception_active so any enclosing try/except
            // handler can observe it. For boto3-base methods
            // (which DO exist on every client) and for
            // forward-references (which DO exist on the class,
            // we just haven't seen the body yet), do NOT set
            // the exception flag — otherwise the call is
            // treated as if it raised AttributeError and the
            // rest of the surrounding statement gets guarded
            // out, hiding subsequent calls and their checks.
            if(
              !is_boto3_base && !declared_on_class &&
              symbol_table.lookup(exc_id) != nullptr)
            {
              code_blockt err_block;
              err_block.add(code_frontend_assignt{
                symbol_table.lookup_ref(exc_id).symbol_expr(), true_exprt{}});
              irep_idt etype_id{"python::__exception_type"};
              if(symbol_table.lookup(etype_id) != nullptr)
              {
                long h = exception_type_hash("AttributeError");
                err_block.add(code_frontend_assignt{
                  symbol_table.lookup_ref(etype_id).symbol_expr(),
                  from_integer(h, python_int_type())});
              }
              // Definitively-unhandled-AttributeError detection.
              // If no enclosing except catches AttributeError (nor a
              // catch-all), emit a dedicated ASSERT false that is
              // NOT suppressed by --python-no-exception-checks —
              // this is a statically-provable bug, not a dynamic
              // exception property.
              //
              // Skipped for forward-references (method declared on
              // the class but body not yet processed) and for
              // boto3 BaseClient inherited methods, since both are
              // false-positive sources for the static check.
              if(!exception_is_caught("AttributeError"))
              {
                source_locationt aloc = get_location(expr);
                aloc.set_property_class("attribute-error");
                aloc.set_comment(
                  "missing method " + class_name + "::" + method_name);
                code_assertt ae{false_exprt{}};
                ae.add_source_location() = aloc;
                code_blockt assert_block;
                assert_block.add(std::move(ae));
                pending_checks.push_back(std::move(assert_block));
              }
              pending_checks.push_back(std::move(err_block));
            }
            log_overapprox(
              "missing method " + class_name + "::" + method_name +
              " — raising AttributeError");
            return side_effect_expr_nondett{
              python_int_type(), get_location(expr)};
          }
        }
      }
      // tag is "python_class_ClassName"
      if(tag.substr(0, 13) == "python_class_")
      {
        std::string class_name = tag.substr(13);
        irep_idt method_id{"python::" + class_name + "::" + method_name};
        const symbolt *method_sym = symbol_table.lookup(method_id);
        if(method_sym != nullptr)
        {
          const code_typet &method_type = to_code_type(method_sym->type);
          exprt::operandst arguments;
          // Pass address of object as self (pointer-based model)
          // Skip for @staticmethod (no self/cls parameter)
          bool has_self = !method_type.parameters().empty() &&
                          method_type.parameters()[0].type().id() == ID_pointer;
          if(has_self)
          {
            if(obj.type().id() == ID_pointer)
              arguments.push_back(obj);
            else if(obj.id() == ID_side_effect)
              arguments.push_back(side_effect_expr_nondett{
                pointer_typet{obj.type(), config.ansi_c.pointer_width},
                get_location(expr)});
            else
              arguments.push_back(address_of_exprt{obj});
          }
          if(args.is_array())
          {
            for(const auto &arg : as_array(args))
              arguments.push_back(convert_expression(arg));
          }

          // Handle keyword arguments for method calls
          const jsont &method_keywords = json_member(expr, "keywords");
          if(method_keywords.is_array() && !as_array(method_keywords).empty())
          {
            const auto &mparams = method_type.parameters();
            arguments.resize(mparams.size(), nil_exprt{});
            std::vector<std::pair<std::string, exprt>> unmatched;
            for(const auto &kw : as_array(method_keywords))
            {
              std::string kw_name = json_string(json_member(kw, "arg"));
              const jsont &kw_value_ast = json_member(kw, "value");
              exprt kw_val = convert_expression(kw_value_ast);

              // Stage 1 of the re-precision plan: regex-no-match
              // check against stub-recorded assertions. For each
              // recorded (kwarg_path, pattern) on the called
              // method, walk the user's kw value AST as a nested
              // Dict descent following kwarg_path[1..]; if it
              // reaches a Constant(""), check pattern's
              // ε-acceptance and emit regex-no-match at the
              // call site if the regex can't accept ε.
              if(!kw_name.empty())
              {
                auto mri = method_regex_asserts.find(method_id);
                if(mri != method_regex_asserts.end())
                {
                  auto descend_dict =
                    [&](
                      const jsont &start,
                      const std::vector<std::string> &path,
                      std::size_t start_idx) -> std::optional<std::string>
                  {
                    const jsont *cur = &start;
                    for(std::size_t i = start_idx; i < path.size(); ++i)
                    {
                      if(!is_node_type(*cur, "Dict"))
                        return std::nullopt;
                      const jsont &dks = json_member(*cur, "keys");
                      const jsont &dvs = json_member(*cur, "values");
                      if(!dks.is_array() || !dvs.is_array())
                        return std::nullopt;
                      auto kit = as_array(dks).begin();
                      auto vit = as_array(dvs).begin();
                      bool found = false;
                      for(; kit != as_array(dks).end() &&
                            vit != as_array(dvs).end();
                          ++kit, ++vit)
                      {
                        if(!is_node_type(*kit, "Constant"))
                          continue;
                        const jsont &kv = json_member(*kit, "value");
                        if(!kv.is_string())
                          continue;
                        if(kv.value == path[i])
                        {
                          cur = &(*vit);
                          found = true;
                          break;
                        }
                      }
                      if(!found)
                        return std::nullopt;
                    }
                    if(is_node_type(*cur, "Constant"))
                    {
                      const jsont &cv = json_member(*cur, "value");
                      if(cv.is_string())
                        return cv.value;
                    }
                    return std::nullopt;
                  };
                  for(const auto &asrt : mri->second)
                  {
                    if(asrt.kwarg_path.empty())
                      continue;
                    if(asrt.kwarg_path.front() != kw_name)
                      continue;
                    auto resolved =
                      descend_dict(kw_value_ast, asrt.kwarg_path, 1);
                    if(resolved.has_value() && resolved->empty())
                    {
                      auto can_eps = python_regex_can_match_empty(asrt.pattern);
                      if(can_eps.has_value() && !*can_eps)
                      {
                        source_locationt rloc = get_location(expr);
                        rloc.set_property_class("regex-no-match");
                        std::string path_str;
                        for(std::size_t i = 1; i < asrt.kwarg_path.size(); ++i)
                          path_str += "[" + asrt.kwarg_path[i] + "]";
                        rloc.set_comment(
                          "regex '" + asrt.pattern +
                          "' cannot match empty string passed via " + kw_name +
                          path_str);
                        code_assertt rne{false_exprt{}};
                        rne.add_source_location() = rloc;
                        code_blockt rne_block;
                        rne_block.add(std::move(rne));
                        pending_checks.push_back(std::move(rne_block));
                      }
                    }
                  }
                }
              }
              // PEP 448: f(**d) — expand known dict-literal
              // contents into individual kw entries.
              if(kw_name.empty())
              {
                const exprt *lit = nullptr;
                if(kw_val.id() == ID_struct && kw_val.operands().size() >= 3)
                  lit = &kw_val;
                else if(kw_val.id() == ID_symbol)
                {
                  auto it =
                    dict_literals.find(to_symbol_expr(kw_val).get_identifier());
                  if(it != dict_literals.end())
                    lit = &it->second;
                }
                if(
                  lit != nullptr && lit->operands().size() >= 3 &&
                  lit->operands()[0].is_constant())
                {
                  mp_integer len_val;
                  if(!to_integer(to_constant_expr(lit->operands()[0]), len_val))
                  {
                    const exprt &keys_arr = lit->operands()[1];
                    const exprt &vals_arr = lit->operands()[2];
                    std::size_t n = len_val.to_ulong();
                    for(std::size_t ii = 0;
                        ii < n && ii < keys_arr.operands().size();
                        ii++)
                    {
                      auto kv = extract_string_value(keys_arr.operands()[ii]);
                      if(!kv.has_value())
                        continue;
                      // --python-check-typeddict-fields: when
                      // the callee is annotated with
                      // **kwargs: Unpack[TypedDict] and the
                      // TypedDict declares a category for this
                      // field, verify the spread value's
                      // static category matches. The category
                      // comes from dict_literal_value_categories
                      // (populated at assignment time from the
                      // original Dict AST), not from the
                      // converted struct, because the struct's
                      // values have been unified via
                      // safe_typecast and lost their original
                      // type identity.
                      if(python_check_typeddict_fields)
                      {
                        auto mu = method_kwargs_unpack.find(method_id);
                        if(
                          mu != method_kwargs_unpack.end() &&
                          kw_val.id() == ID_symbol)
                        {
                          auto tf = typed_dict_field_types.find(mu->second);
                          auto dc = dict_literal_value_categories.find(
                            to_symbol_expr(kw_val).get_identifier());
                          if(
                            tf != typed_dict_field_types.end() &&
                            dc != dict_literal_value_categories.end())
                          {
                            auto fi = tf->second.find(kv.value());
                            auto vci = dc->second.find(kv.value());
                            if(
                              fi != tf->second.end() && vci != dc->second.end())
                            {
                              const std::string &expected = fi->second;
                              const std::string &vc = vci->second;
                              if(
                                expected != "tuple" && vc != expected &&
                                // bool is acceptable where int
                                // is expected (Python: bool ⊆
                                // int).
                                !(expected == "int" && vc == "bool") &&
                                // int implicitly convertible to
                                // float.
                                !(expected == "float" &&
                                  (vc == "int" || vc == "bool")))
                              {
                                source_locationt tloc = get_location(expr);
                                tloc.set_property_class("type-error");
                                tloc.set_comment(
                                  "TypedDict field '" + kv.value() +
                                  "' expects " + expected + ", got " + vc);
                                code_assertt te{false_exprt{}};
                                te.add_source_location() = tloc;
                                code_blockt te_block;
                                te_block.add(std::move(te));
                                pending_checks.push_back(std::move(te_block));
                              }
                            }
                          }
                        }
                      }
                      bool mm = false;
                      for(std::size_t jj = 0; jj < mparams.size(); jj++)
                      {
                        if(id2string(mparams[jj].get_base_name()) == kv.value())
                        {
                          // Typecast the spread value to the
                          // matched parameter's declared type.
                          // Without this, the dict's value-array
                          // element type (commonly python_value
                          // for dict[str, object]) leaks into
                          // an argument slot typed differently
                          // (e.g. python_string), which trips
                          // CBMC's equal_exprt invariant when
                          // it builds eq(arg, symex::args::N)
                          // during convert_function_calls.
                          exprt v = vals_arr.operands()[ii];
                          if(v.type() != mparams[jj].type())
                            v = safe_typecast(v, mparams[jj].type());
                          arguments[jj] = std::move(v);
                          mm = true;
                          break;
                        }
                      }
                      if(!mm)
                        unmatched.push_back(
                          {kv.value(), vals_arr.operands()[ii]});
                    }
                    continue;
                  }
                }
                log_overapprox(
                  "**dict spread of non-literal dict in method call — "
                  "kwargs under-populated");
                continue;
              }
              bool matched = false;
              for(std::size_t i = 0; i < mparams.size(); i++)
              {
                if(id2string(mparams[i].get_base_name()) == kw_name)
                {
                  exprt v = kw_val;
                  if(v.type() != mparams[i].type())
                    v = safe_typecast(v, mparams[i].type());
                  arguments[i] = std::move(v);
                  matched = true;
                  break;
                }
              }
              if(!matched)
              {
                // --python-check-typeddict-fields: when this
                // kwarg goes to **kwargs of a stub method
                // annotated with Unpack[TypedDict] and the
                // TypedDict declares a category for this
                // field, verify the value's static category
                // matches. Only fires for genuine kw=val
                // (non-spread) arguments — values from PEP
                // 448 spread are checked at the spread
                // expansion site against the per-key AST
                // category (the value-array element type
                // would be the dict's homogenized val_type
                // and unreliable here).
                if(python_check_typeddict_fields)
                {
                  auto mu = method_kwargs_unpack.find(method_id);
                  if(mu != method_kwargs_unpack.end())
                  {
                    auto tf = typed_dict_field_types.find(mu->second);
                    if(tf != typed_dict_field_types.end())
                    {
                      auto fi = tf->second.find(kw_name);
                      if(fi != tf->second.end())
                      {
                        auto exprt_category = [&](const exprt &e_in)
                        {
                          exprt e = e_in;
                          while(e.id() == ID_typecast &&
                                e.operands().size() == 1)
                            e = e.operands()[0];
                          if(is_python_string_type(e.type()))
                            return std::string{"str"};
                          if(is_python_list_type(e.type()))
                            return std::string{"list"};
                          if(is_python_dict_type(e.type()))
                            return std::string{"dict"};
                          if(is_python_set_type(e.type()))
                            return std::string{"set"};
                          if(e.type().id() == ID_bool)
                            return std::string{"bool"};
                          if(
                            e.type().id() == ID_signedbv ||
                            e.type().id() == ID_unsignedbv ||
                            e.type().id() == ID_integer)
                            return std::string{"int"};
                          if(e.type().id() == ID_floatbv)
                            return std::string{"float"};
                          return std::string{};
                        };
                        const std::string &expected = fi->second;
                        std::string vc = exprt_category(kw_val);
                        if(
                          !vc.empty() && expected != "tuple" &&
                          vc != expected &&
                          !(expected == "int" && vc == "bool") &&
                          !(expected == "float" &&
                            (vc == "int" || vc == "bool")))
                        {
                          source_locationt tloc = get_location(expr);
                          tloc.set_property_class("type-error");
                          tloc.set_comment(
                            "TypedDict field '" + kw_name + "' expects " +
                            expected + ", got " + vc);
                          code_assertt te{false_exprt{}};
                          te.add_source_location() = tloc;
                          code_blockt te_block;
                          te_block.add(std::move(te));
                          pending_checks.push_back(std::move(te_block));
                        }
                      }
                    }
                  }
                }
                unmatched.push_back({kw_name, kw_val});
              }
            }
            if(
              !unmatched.empty() && !mparams.empty() &&
              is_python_dict_type(mparams.back().type()))
            {
              std::size_t ki = mparams.size() - 1;
              typet dt = mparams.back().type();
              const auto &dst = to_struct_type(dt);
              const auto &kat = to_array_type(dst.components()[1].type());
              const auto &vat = to_array_type(dst.components()[2].type());
              exprt::operandst ks, vs;
              for(const auto &[n, v] : unmatched)
              {
                ks.push_back(python_string_literal(n));
                if(is_python_value_type(vat.element_type()))
                  vs.push_back(wrap_value(v));
                else
                  vs.push_back(safe_typecast(v, vat.element_type()));
              }
              while(ks.size() < PYTHON_MAX_DICT_SIZE)
              {
                ks.push_back(safe_zero(kat.element_type()));
                vs.push_back(safe_zero(vat.element_type()));
              }
              arguments[ki] = struct_exprt{
                {from_integer(
                   static_cast<long long>(unmatched.size()),
                   signedbv_typet{64}),
                 array_exprt{std::move(ks), kat},
                 array_exprt{std::move(vs), vat}},
                dt};
            }
            for(std::size_t i = 0; i < arguments.size() && i < mparams.size();
                i++)
            {
              if(arguments[i].is_nil())
                arguments[i] = safe_zero(mparams[i].type());
              else if(arguments[i].type() != mparams[i].type())
                arguments[i] = safe_typecast(arguments[i], mparams[i].type());
            }
          }

          // Type-consistency fixup for arguments. This runs
          // unconditionally (whether or not the call had
          // keyword arguments) because positional arguments
          // can also be NIL when convert_expression on a
          // sub-expression — e.g. convert_subscript falling
          // through on an unsupported operand — returns
          // nil_exprt{}. Without this fixup, a NIL argument
          // sneaks into the goto's FUNCTION_CALL and trips
          // CBMC's equal_exprt invariant during SSA-step
          // function-argument introduction (see
          // symex_target_equation::convert_function_calls).
          {
            const auto &fp = method_type.parameters();
            for(std::size_t i = 0; i < arguments.size() && i < fp.size(); i++)
            {
              if(arguments[i].is_nil())
                arguments[i] = safe_zero(fp[i].type());
              else if(arguments[i].type() != fp[i].type())
                arguments[i] = safe_typecast(arguments[i], fp[i].type());
            }
          }

          // Check for dynamic dispatch: if subclasses override this method,
          // dispatch based on __class_tag
          std::vector<std::pair<std::string, irep_idt>> dispatch_targets;
          for(const auto &[sub_name, sub_bases] : class_bases)
          {
            for(const auto &base : sub_bases)
            {
              if(base == class_name)
              {
                irep_idt sub_method_id{
                  "python::" + sub_name + "::" + method_name};
                if(symbol_table.lookup(sub_method_id) != nullptr)
                  dispatch_targets.emplace_back(sub_name, sub_method_id);
                break;
              }
            }
          }

          if(!dispatch_targets.empty())
          {
            // Read __class_tag from the object
            exprt deref_obj =
              obj.type().id() == ID_pointer ? dereference_exprt{obj} : obj;
            exprt tag_field =
              member_exprt{deref_obj, "__class_tag", signedbv_typet{32}};

            // Build if-then-else chain: check subclass tags first
            exprt result_call = side_effect_expr_function_callt{
              method_sym->symbol_expr(),
              arguments,
              method_type.return_type(),
              get_location(expr)};

            for(auto it = dispatch_targets.rbegin();
                it != dispatch_targets.rend();
                ++it)
            {
              const auto &[sub_name, sub_method_id] = *it;
              const symbolt &sub_sym = symbol_table.lookup_ref(sub_method_id);
              exprt sub_call = side_effect_expr_function_callt{
                sub_sym.symbol_expr(),
                arguments,
                method_type.return_type(),
                get_location(expr)};
              exprt tag_check = equal_exprt{
                tag_field,
                from_integer(class_tag_ids[sub_name], signedbv_typet{32})};
              result_call = if_exprt{tag_check, sub_call, result_call};
            }
            return result_call;
          }

          side_effect_expr_function_callt call{
            method_sym->symbol_expr(),
            std::move(arguments),
            method_type.return_type(),
            get_location(expr)};
          return std::move(call);
        }
      }
    }
    // Suppress warnings for known regex/common methods on nondet objects
    if(
      method_name != "search" && method_name != "match" &&
      method_name != "group" && method_name != "groups" &&
      method_name != "span" && method_name != "findall" &&
      method_name != "sub" && method_name != "split" &&
      method_name != "compile" && method_name != "pattern" &&
      // Common container/string methods whose effect is opaque at
      // the Python-front-end level — they already fall through to a
      // nondet result. Silencing them reduces log noise for stdlib
      // ingestion (Step 1 of module support plan).
      method_name != "__class__" && method_name != "__new__" &&
      method_name != "__init__" && method_name != "__del__" &&
      method_name != "__cast" && method_name != "items" &&
      method_name != "keys" && method_name != "values" &&
      method_name != "get" && method_name != "pop" && method_name != "update" &&
      method_name != "clear" && method_name != "add" &&
      method_name != "discard" && method_name != "remove" &&
      method_name != "insert" && method_name != "append" &&
      method_name != "extend" && method_name != "copy" &&
      method_name != "read" && method_name != "write" &&
      method_name != "close" && method_name != "flush" &&
      method_name != "seek" && method_name != "tell" && method_name != "join" &&
      method_name != "encode" && method_name != "decode" &&
      method_name != "startswith" && method_name != "endswith" &&
      method_name != "strip" && method_name != "rstrip" &&
      method_name != "lstrip" && method_name != "replace" &&
      method_name != "format" && method_name != "split" &&
      method_name != "rsplit" && method_name != "rpartition" &&
      method_name != "partition" && method_name != "find" &&
      method_name != "rfind" && method_name != "index" &&
      method_name != "rindex" && method_name != "count" &&
      method_name != "lower" && method_name != "upper" &&
      method_name != "title" && method_name != "capitalize" &&
      method_name != "swapcase" && method_name != "isdigit" &&
      method_name != "isalpha" && method_name != "isalnum" &&
      method_name != "isspace" && method_name != "islower" &&
      method_name != "isupper" && method_name != "translate" &&
      method_name != "maketrans" && method_name != "zfill" &&
      method_name != "center" && method_name != "ljust" &&
      method_name != "rjust" && method_name != "expandtabs")
      log_overapprox(
        "method '" + method_name +
        "': no resolution, returning nondet over-approximation");
    // Stub-context fallback for regex method names. When code
    // like ``compile(r).search(v)`` appears in an imported stub
    // (e.g. third-party library stubs under PYTHONPATH), neither
    // ``compile`` nor ``search`` resolves to our library's re
    // model — the stub's import happens in a context where
    // module resolution doesn't reach cbmc-python.git's built-in
    // library directory. The caller's typical check pattern is
    // ``search(v) is not None``; to keep that provable we
    // constrain the nondet result to be non-negative, which
    // excludes the None sentinel (-2^62). This mirrors the
    // pre-Wave-1 ad-hoc behaviour that we'd retired once the
    // library stub took over — for USER code the library still
    // provides the real model; this fallback only helps stub-
    // resident calls.
    if(
      method_name == "search" || method_name == "match" ||
      method_name == "fullmatch" || method_name == "compile" ||
      method_name == "findall" || method_name == "finditer" ||
      method_name == "sub" || method_name == "subn" || method_name == "split")
    {
      side_effect_expr_nondett nd{python_int_type(), get_location(expr)};
      static unsigned re_stub_ctr = 0;
      std::string tn = "__re_stub_result_" + std::to_string(re_stub_ctr++);
      std::string tq = qualify_name(tn);
      irep_idt ti{tq};
      if(symbol_table.lookup(ti) == nullptr)
      {
        symbolt ts{ti, python_int_type(), "python"};
        ts.base_name = tn;
        ts.is_lvalue = true;
        ts.is_state_var = true;
        symbol_table.add(ts);
      }
      const symbolt &ts = symbol_table.lookup_ref(ti);
      pending_checks.push_back(code_frontend_assignt{ts.symbol_expr(), nd});
      pending_checks.push_back(code_assumet{binary_relation_exprt{
        ts.symbol_expr(), ID_ge, from_integer(0, python_int_type())}});
      return ts.symbol_expr();
    }
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }

  // Handle nondet functions
  if(func_name == "nondet_int" || func_name == "__VERIFIER_nondet_int")
  {
    side_effect_expr_nondett nondet{python_int_type(), get_location(expr)};
    return std::move(nondet);
  }
  // Frontend hooks for the Python library's re stub.
  // ``__cbmc_re_{match,search,fullmatch}(pattern, subject)`` are the
  // three entry points the library calls when both arguments are
  // Python strings. The front-end lowers each to the corresponding
  // ``cprover_string_{match,search,fullmatch}_func`` intrinsic; the
  // SMT backend (in particular ``--cvc5``) intercepts the intrinsic
  // and compiles the pattern to an SMT-LIB 2.6 regex term. When
  // the pattern cannot be translated (back-refs, lookaround, ...)
  // the intrinsic degrades to a sound nondet result.
  if(
    func_name == "__cbmc_re_match" || func_name == "__cbmc_re_search" ||
    func_name == "__cbmc_re_fullmatch")
  {
    if(args.is_array() && as_array(args).size() == 2)
    {
      auto it = as_array(args).begin();
      exprt pattern = convert_expression(*it++);
      exprt subject = convert_expression(*it);
      if(
        is_python_string_type(pattern.type()) &&
        is_python_string_type(subject.type()))
      {
        auto to_str = [](const exprt &s) -> exprt
        {
          if(s.id() == ID_struct && s.operands().size() == 2)
            return s;
          return struct_exprt{
            {member_exprt{s, "length", signedbv_typet{64}},
             member_exprt{s, "data", pointer_typet{unsignedbv_typet{8}, 64}}},
            s.type()};
        };
        irep_idt intrinsic_id = ID_cprover_string_match_func;
        if(func_name == "__cbmc_re_search")
          intrinsic_id = ID_cprover_string_search_func;
        else if(func_name == "__cbmc_re_fullmatch")
          intrinsic_id = ID_cprover_string_fullmatch_func;
        return emit_string_bool_function(
          intrinsic_id,
          to_str(pattern),
          to_str(subject),
          symbol_table,
          pending_checks);
      }
    }
    // Fallback: nondet bool.
    return side_effect_expr_nondett{bool_typet{}, get_location(expr)};
  }
  else if(func_name == "nondet_float" || func_name == "__VERIFIER_nondet_float")
  {
    side_effect_expr_nondett nondet{double_type(), get_location(expr)};
    return std::move(nondet);
  }
  else if(func_name == "nondet_bool" || func_name == "__VERIFIER_nondet_bool")
  {
    side_effect_expr_nondett nondet{bool_typet{}, get_location(expr)};
    return std::move(nondet);
  }
  else if(func_name == "randint")
  {
    // from random import randint — constrained nondet
    if(args.is_array() && as_array(args).size() >= 2)
    {
      auto it = as_array(args).begin();
      exprt lo = convert_expression(*it);
      ++it;
      exprt hi = convert_expression(*it);
      side_effect_expr_nondett nondet{python_int_type(), get_location(expr)};
      static unsigned ri_ctr = 0;
      std::string tn = "__randint_" + std::to_string(ri_ctr++);
      std::string tq = qualify_name(tn);
      irep_idt ti{tq};
      if(symbol_table.lookup(ti) == nullptr)
      {
        symbolt ts{ti, python_int_type(), "python"};
        ts.base_name = tn;
        ts.is_lvalue = true;
        ts.is_state_var = true;
        symbol_table.add(ts);
      }
      symbol_exprt tv = symbol_table.lookup_ref(ti).symbol_expr();
      pending_checks.push_back(code_frontend_assignt{tv, nondet});
      pending_checks.push_back(
        code_assumet{binary_relation_exprt{tv, ID_ge, lo}});
      pending_checks.push_back(
        code_assumet{binary_relation_exprt{tv, ID_le, hi}});
      return tv;
    }
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  else if(func_name == "nondet_str" || func_name == "nondet_string")
  {
    static unsigned ns_ctr = 0;
    std::string tn = "__nondet_str_" + std::to_string(ns_ctr++);
    std::string tq = qualify_name(tn);
    irep_idt ti{tq};
    if(symbol_table.lookup(ti) == nullptr)
    {
      symbolt ts{ti, python_string_type(), "python"};
      ts.base_name = tn;
      ts.is_lvalue = true;
      ts.is_state_var = true;
      symbol_table.add(ts);
    }
    symbol_exprt tmp = symbol_table.lookup_ref(ti).symbol_expr();
    pending_checks.push_back(code_frontend_assignt{
      tmp, side_effect_expr_nondett{python_string_type(), get_location(expr)}});
    // Emit the length constraint through the intrinsic so
    // downstream len() consumers (which route through
    // cprover_string_length_func) see the same value.
    exprt len_intr = emit_string_int_function(
      ID_cprover_string_length_func, tmp, symbol_table, pending_checks);
    // If size argument provided, constrain length == size
    if(args.is_array() && !as_array(args).empty())
    {
      exprt size = convert_expression(*as_array(args).begin());
      exprt size_i64 = safe_typecast(size, signedbv_typet{64});
      pending_checks.push_back(code_assumet{equal_exprt{len_intr, size_i64}});
    }
    else
    {
      pending_checks.push_back(code_assumet{and_exprt{
        binary_relation_exprt{
          len_intr, ID_ge, from_integer(0, signedbv_typet{64})},
        binary_relation_exprt{
          len_intr,
          ID_le,
          from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64})}}});
    }
    return std::move(tmp);
  }
  else if(func_name == "nondet_list")
  {
    // nondet_list(n) — constrain length to [0, n]
    static unsigned nl_ctr = 0;
    exprt max_len = from_integer(PYTHON_MAX_LIST_LENGTH, signedbv_typet{64});
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(arg.type().id() != ID_signedbv)
        arg = safe_typecast(arg, signedbv_typet{64});
      max_len = arg;
    }
    std::string tn = "__nondet_list_" + std::to_string(nl_ctr++);
    std::string tq = qualify_name(tn);
    irep_idt ti{tq};
    typet lt = python_list_type(python_int_type());
    if(symbol_table.lookup(ti) == nullptr)
    {
      symbolt ts{ti, lt, "python"};
      ts.base_name = tn;
      ts.is_lvalue = true;
      ts.is_state_var = true;
      symbol_table.add(ts);
    }
    symbol_exprt tmp = symbol_table.lookup_ref(ti).symbol_expr();
    pending_checks.push_back(code_frontend_assignt{
      tmp, side_effect_expr_nondett{lt, get_location(expr)}});
    member_exprt len{tmp, "length", signedbv_typet{64}};
    pending_checks.push_back(code_assumet{and_exprt{
      binary_relation_exprt{len, ID_ge, from_integer(0, signedbv_typet{64})},
      binary_relation_exprt{len, ID_le, max_len}}});
    return std::move(tmp);
  }
  else if(func_name == "nondet_dict")
  {
    typet dt = python_dict_type(python_string_type(), python_int_type());
    static unsigned nd_ctr = 0;
    exprt max_len = from_integer(PYTHON_MAX_DICT_SIZE, signedbv_typet{64});
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(arg.type().id() != ID_signedbv)
        arg = safe_typecast(arg, signedbv_typet{64});
      max_len = arg;
    }
    std::string tn = "__nondet_dict_" + std::to_string(nd_ctr++);
    std::string tq = qualify_name(tn);
    irep_idt ti{tq};
    if(symbol_table.lookup(ti) == nullptr)
    {
      symbolt ts{ti, dt, "python"};
      ts.base_name = tn;
      ts.is_lvalue = true;
      ts.is_state_var = true;
      symbol_table.add(ts);
    }
    symbol_exprt tmp = symbol_table.lookup_ref(ti).symbol_expr();
    pending_checks.push_back(code_frontend_assignt{
      tmp, side_effect_expr_nondett{dt, get_location(expr)}});
    member_exprt len{tmp, "length", signedbv_typet{64}};
    pending_checks.push_back(code_assumet{and_exprt{
      binary_relation_exprt{len, ID_ge, from_integer(0, signedbv_typet{64})},
      binary_relation_exprt{len, ID_le, max_len}}});
    return std::move(tmp);
  }
  else if(func_name == "nondet_complex")
  {
    struct_typet::componentst comps;
    comps.push_back(struct_typet::componentt{"real", double_type()});
    comps.push_back(struct_typet::componentt{"imag", double_type()});
    struct_typet ct{comps};
    ct.set_tag("python_complex");
    return side_effect_expr_nondett{ct, get_location(expr)};
  }
  // PLR: assume() constrains nondet values (ESBMC/CBMC verification primitive)
  else if(
    func_name == "assume" || func_name == "__VERIFIER_assume" ||
    func_name == "__CPROVER_assume" || func_name == "__ESBMC_assume")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt cond = convert_expression(*as_array(args).begin());
      if(cond.type().id() != ID_bool)
        cond = typecast_exprt{cond, bool_typet{}};
      pending_checks.push_back(code_assumet{cond});
    }
    return from_integer(0, python_int_type());
  }
  // PLib builtins: map(func, iterable)
  else if(func_name == "map")
  {
    if(args.is_array() && as_array(args).size() >= 2)
    {
      auto it = as_array(args).begin();
      exprt func_arg = convert_expression(*it);
      ++it;
      exprt list_arg = convert_expression(*it);
      if(
        is_python_list_type(list_arg.type()) && func_arg.id() == ID_symbol &&
        func_arg.type().id() == ID_code)
      {
        // Unroll: result[i] = func(input[i]) for i in 0..length
        const auto &list_st = to_struct_type(list_arg.type());
        const auto &data_type = to_array_type(list_st.components()[1].type());
        const code_typet &ft = to_code_type(func_arg.type());
        typet ret_type = ft.return_type();
        typet result_list_type = python_list_type(ret_type);

        static unsigned map_ctr = 0;
        std::string tn = "__map_" + std::to_string(map_ctr++);
        std::string tq = qualify_name(tn);
        irep_idt ti{tq};
        if(symbol_table.lookup(ti) == nullptr)
        {
          symbolt ts{ti, result_list_type, "python"};
          ts.base_name = tn;
          ts.is_lvalue = true;
          ts.is_state_var = true;
          symbol_table.add(ts);
        }
        symbol_exprt tmp = symbol_table.lookup_ref(ti).symbol_expr();
        member_exprt src_data{list_arg, "data", data_type};
        member_exprt src_len{list_arg, "length", signedbv_typet{64}};
        const auto &res_data_type = to_array_type(
          to_struct_type(result_list_type).components()[1].type());
        member_exprt dst_data{tmp, "data", res_data_type};

        pending_checks.push_back(code_frontend_assignt{
          member_exprt{tmp, "length", signedbv_typet{64}}, src_len});

        for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
        {
          exprt idx = from_integer(i, signedbv_typet{64});
          exprt guard = binary_relation_exprt{idx, ID_lt, src_len};
          exprt elem = index_exprt{src_data, idx};
          if(elem.type() != ft.parameters()[0].type())
            elem = safe_typecast(elem, ft.parameters()[0].type());
          side_effect_expr_function_callt call{
            func_arg, {elem}, ret_type, get_location(expr)};
          pending_checks.push_back(code_ifthenelset{
            guard, code_frontend_assignt{index_exprt{dst_data, idx}, call}});
        }
        return std::move(tmp);
      }
      if(is_python_list_type(list_arg.type()))
        return side_effect_expr_nondett{list_arg.type(), get_location(expr)};
    }
    return side_effect_expr_nondett{
      python_list_type(python_int_type()), get_location(expr)};
  }
  // PLib builtins: zip(*iterables)
  else if(func_name == "zip")
  {
    // Return nondet list of tuples
    return side_effect_expr_nondett{
      python_list_type(python_int_type()), get_location(expr)};
  }
  // PLib builtins: filter(func, iterable)
  else if(func_name == "filter")
  {
    if(args.is_array() && as_array(args).size() >= 2)
    {
      auto it = as_array(args).begin();
      exprt func_arg = convert_expression(*it);
      ++it;
      exprt list_arg = convert_expression(*it);
      if(
        is_python_list_type(list_arg.type()) && func_arg.id() == ID_symbol &&
        func_arg.type().id() == ID_code)
      {
        const auto &list_st = to_struct_type(list_arg.type());
        const auto &data_type = to_array_type(list_st.components()[1].type());
        const code_typet &ft = to_code_type(func_arg.type());
        typet result_list_type = list_arg.type();
        member_exprt src_data{list_arg, "data", data_type};
        member_exprt src_len{list_arg, "length", signedbv_typet{64}};

        static unsigned filter_ctr = 0;
        std::string tn = "__filter_" + std::to_string(filter_ctr++);
        std::string tq = qualify_name(tn);
        irep_idt ti{tq};
        if(symbol_table.lookup(ti) == nullptr)
        {
          symbolt ts{ti, result_list_type, "python"};
          ts.base_name = tn;
          ts.is_lvalue = true;
          ts.is_state_var = true;
          symbol_table.add(ts);
        }
        symbol_exprt tmp = symbol_table.lookup_ref(ti).symbol_expr();
        const auto &res_data_type = to_array_type(
          to_struct_type(result_list_type).components()[1].type());
        member_exprt dst_data{tmp, "data", res_data_type};

        // Counter for result length
        std::string cn = tn + "_len";
        std::string cq = qualify_name(cn);
        irep_idt ci{cq};
        if(symbol_table.lookup(ci) == nullptr)
        {
          symbolt cs{ci, python_int_type(), "python"};
          cs.base_name = cn;
          cs.is_lvalue = true;
          cs.is_state_var = true;
          symbol_table.add(cs);
        }
        symbol_exprt cnt = symbol_table.lookup_ref(ci).symbol_expr();
        pending_checks.push_back(
          code_frontend_assignt{cnt, from_integer(0, python_int_type())});

        for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
        {
          exprt idx = from_integer(i, signedbv_typet{64});
          exprt guard = binary_relation_exprt{idx, ID_lt, src_len};
          exprt elem = index_exprt{src_data, idx};
          if(elem.type() != ft.parameters()[0].type())
            elem = safe_typecast(elem, ft.parameters()[0].type());
          side_effect_expr_function_callt call{
            func_arg, {elem}, ft.return_type(), get_location(expr)};
          // If filter returns truthy, add to result
          exprt truthy = safe_typecast(call, bool_typet{});
          exprt include = and_exprt{guard, truthy};
          pending_checks.push_back(code_ifthenelset{
            include,
            code_blockt{
              {code_frontend_assignt{index_exprt{dst_data, cnt}, elem},
               code_frontend_assignt{
                 cnt, plus_exprt{cnt, from_integer(1, python_int_type())}}}}});
        }
        pending_checks.push_back(code_frontend_assignt{
          member_exprt{tmp, "length", python_int_type()}, cnt});
        return std::move(tmp);
      }
      if(is_python_list_type(list_arg.type()))
        return side_effect_expr_nondett{list_arg.type(), get_location(expr)};
    }
    return side_effect_expr_nondett{
      python_list_type(python_int_type()), get_location(expr)};
  }
  // iter(x) returns x (for lists, iteration is by index)
  // next(it) returns first element (simplified model)
  else if(func_name == "iter")
  {
    if(args.is_array() && !as_array(args).empty())
      return convert_expression(*as_array(args).begin());
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  else if(func_name == "next")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(!arg.is_nil() && is_python_list_type(arg.type()))
      {
        const auto &data_type =
          to_array_type(to_struct_type(arg.type()).components()[1].type());
        return index_exprt{
          member_exprt{arg, "data", data_type},
          from_integer(0, signedbv_typet{64})};
      }
    }
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  else if(func_name == "len")
  {
    // PLib builtins: len(s) returns the length of s
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(!arg.is_nil())
      {
        // Python string: route through the refinement intrinsic
        // so the back-end (refine-strings or future SMT strings)
        // controls the semantics.
        if(is_python_string_type(arg.type()))
        {
          exprt result = emit_string_int_function(
            ID_cprover_string_length_func, arg, symbol_table, pending_checks);
          if(result.type() != python_int_type())
            result = safe_typecast(result, python_int_type());
          return result;
        }
        if(is_python_list_type(arg.type()) || is_python_dict_type(arg.type()))
          return member_exprt{arg, "length", python_int_type()};

        // Tuple: number of components
        if(is_python_tuple_type(arg.type()))
        {
          const auto &st = to_struct_type(arg.type());
          return from_integer(st.components().size(), python_int_type());
        }

        // Set (bitmap): popcount
        if(is_python_set_type(arg.type()))
        {
          // Approximate: count bits in bitmap
          member_exprt bm{arg, "bitmap", unsignedbv_typet{64}};
          return popcount_exprt{bm, python_int_type()};
        }

        // Tagged union: dispatch on tag
        if(is_python_value_type(arg.type()))
        {
          exprt str_len =
            member_exprt{python_value_str(arg), "length", python_int_type()};
          exprt list_len =
            member_exprt{python_value_list(arg), "length", python_int_type()};
          // DICT tag: the pointed-to dict struct has .length at
          // offset 0 (signedbv[64]). CLASS tag: the pointed-to
          // class instance has __class_tag (signedbv[32]) at
          // offset 0 — reading 8 bytes there is garbage, so we
          // only apply the length-read trick for DICT-tagged
          // values. CLASS-tagged values without __len__ should
          // raise TypeError; we over-approximate as nondet int.
          pointer_typet len_ptr_type{signedbv_typet{64}, 64};
          exprt dict_len = typecast_exprt{
            dereference_exprt{
              typecast_exprt{python_value_class_ptr(arg), len_ptr_type},
              signedbv_typet{64}},
            python_int_type()};
          return if_exprt{
            python_value_is(arg, python_type_tagt::STR),
            str_len,
            if_exprt{
              python_value_is(arg, python_type_tagt::LIST),
              list_len,
              if_exprt{
                python_value_is(arg, python_type_tagt::DICT),
                dict_len,
                side_effect_expr_nondett{
                  python_int_type(), source_locationt{}}}}};
        }
      }
    }
    // Check for __len__ dunder method on class instances
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(!arg.is_nil() && arg.type().id() == ID_struct)
      {
        std::string tag = id2string(to_struct_type(arg.type()).get_tag());
        // Try python_class_X::__len__ or just X::__len__
        for(const auto &prefix :
            {"python::" + tag + "::__len__",
             "python::" +
               (tag.substr(0, 13) == "python_class_" ? tag.substr(13) : tag) +
               "::__len__"})
        {
          const symbolt *len_sym = symbol_table.lookup(irep_idt{prefix});
          if(len_sym != nullptr)
            return side_effect_expr_function_callt{
              len_sym->symbol_expr(),
              {address_of_exprt{arg}},
              python_int_type(),
              get_location(expr)};
        }
      }
    }
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  // Built-in type constructors: int(), float(), bool()
  else if(func_name == "int")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(!arg.is_nil())
      {
        // Tagged union: dispatch on tag
        if(is_python_value_type(arg.type()))
          return unwrap_value(arg, python_int_type());
        // int("60") — parse constant string to int.
        // PLR builtins: int(x, base=10) parses the string
        // representation, accepting leading/trailing
        // whitespace and an optional sign.
        if(is_python_string_type(arg.type()))
        {
          auto sv = extract_string_value(arg);
          if(sv.has_value())
          {
            try
            {
              long long val = std::stoll(sv.value());
              return from_integer(val, python_int_type());
            }
            catch(...)
            {
            }
          }
          // Symbolic string: emit cprover_string_parse_int_func
          // so the solver knows the int's relationship to the
          // string's characters. Inverse of str(n) / f"{n}" /
          // "{}".format(n) which emit cprover_string_of_int_func.
          exprt parsed = emit_string_int_function(
            ID_cprover_string_parse_int_func,
            arg,
            symbol_table,
            pending_checks);
          if(parsed.type() != python_int_type())
            parsed = safe_typecast(parsed, python_int_type());
          return parsed;
        }
        return safe_typecast(arg, python_int_type());
      }
    }
    return from_integer(0, python_int_type());
  }
  else if(func_name == "float")
  {
    // PLR §2.4.8: float(x) converts x to floating-point
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(!arg.is_nil())
      {
        // For constant integers, use ieee_floatt for exact conversion
        if(
          arg.is_constant() &&
          (arg.type().id() == ID_signedbv || arg.type().id() == ID_unsignedbv))
        {
          mp_integer iv;
          if(!to_integer(to_constant_expr(arg), iv))
          {
            ieee_floatt fv{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            fv.from_integer(iv);
            return fv.to_expr();
          }
        }
        // float("60") — parse constant string to float
        if(is_python_string_type(arg.type()))
        {
          auto sv = extract_string_value(arg);
          if(sv.has_value())
          {
            try
            {
              ieee_floatt fv{
                ieee_float_spect::double_precision(),
                ieee_floatt::rounding_modet::ROUND_TO_EVEN};
              fv.from_double(std::stod(sv.value()));
              return fv.to_expr();
            }
            catch(...)
            {
            }
          }
        }
        return typecast_exprt{arg, double_type()};
      }
    }
    ieee_floatt zero{
      ieee_float_spect::double_precision(),
      ieee_floatt::rounding_modet::ROUND_TO_EVEN};
    return zero.to_expr();
  }
  else if(func_name == "bool")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(!arg.is_nil())
      {
        // Check for __bool__ dunder
        if(arg.type().id() == ID_struct)
        {
          std::string tag = id2string(to_struct_type(arg.type()).get_tag());
          std::string cls =
            tag.substr(0, 13) == "python_class_" ? tag.substr(13) : tag;
          irep_idt bid{"python::" + cls + "::__bool__"};
          const symbolt *bsym = symbol_table.lookup(bid);
          if(bsym != nullptr)
            return side_effect_expr_function_callt{
              bsym->symbol_expr(),
              {address_of_exprt{arg}},
              bool_typet{},
              get_location(expr)};
        }
        return safe_typecast(arg, bool_typet{});
      }
    }
    return false_exprt{};
  }
  // print() — no-op, return None (modeled as 0)
  else if(func_name == "print")
  {
    mp_integer none_val = mp_integer(1) << 62;
    none_val = -none_val;
    return from_integer(none_val, python_int_type());
  }
  // PLib builtins: input() reads from stdin — model as nondet string
  else if(func_name == "input")
  {
    return side_effect_expr_nondett{python_string_type(), get_location(expr)};
  }
  // PLib builtins: hex/oct/bin — compute for constants, nondet otherwise
  else if(func_name == "hex" || func_name == "oct" || func_name == "bin")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      // Evaluate unary minus on constants
      if(
        arg.id() == ID_unary_minus && arg.operands().size() == 1 &&
        arg.operands()[0].is_constant())
      {
        mp_integer v;
        if(!to_integer(to_constant_expr(arg.operands()[0]), v))
          arg = from_integer(-v, arg.type());
      }
      if(arg.is_constant() && arg.type().id() == ID_signedbv)
      {
        mp_integer val;
        if(!to_integer(to_constant_expr(arg), val))
        {
          std::string result;
          bool negative = val < 0;
          mp_integer abs_val = negative ? -val : val;
          if(func_name == "hex")
          {
            std::string digits;
            if(abs_val == 0)
              digits = "0";
            else
            {
              mp_integer tmp = abs_val;
              while(tmp > 0)
              {
                int d = (tmp % 16).to_long();
                digits = std::string(1, "0123456789abcdef"[d]) + digits;
                tmp /= 16;
              }
            }
            result = (negative ? "-0x" : "0x") + digits;
          }
          else if(func_name == "oct")
          {
            std::string digits;
            if(abs_val == 0)
              digits = "0";
            else
            {
              mp_integer tmp = abs_val;
              while(tmp > 0)
              {
                digits = std::to_string((tmp % 8).to_long()) + digits;
                tmp /= 8;
              }
            }
            result = (negative ? "-0o" : "0o") + digits;
          }
          else // bin
          {
            std::string digits;
            if(abs_val == 0)
              digits = "0";
            else
            {
              mp_integer tmp = abs_val;
              while(tmp > 0)
              {
                digits = ((tmp % 2) == 1 ? "1" : "0") + digits;
                tmp /= 2;
              }
            }
            result = (negative ? "-0b" : "0b") + digits;
          }
          return python_string_literal(result);
        }
      }
    }
    return side_effect_expr_nondett{python_string_type(), get_location(expr)};
  }
  else if(func_name == "repr" || func_name == "ascii")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(!arg.is_nil() && arg.type().id() == ID_struct)
      {
        std::string tag = id2string(to_struct_type(arg.type()).get_tag());
        std::string cls =
          tag.substr(0, 13) == "python_class_" ? tag.substr(13) : tag;
        irep_idt rid{"python::" + cls + "::__repr__"};
        const symbolt *rsym = symbol_table.lookup(rid);
        if(rsym != nullptr)
          return side_effect_expr_function_callt{
            rsym->symbol_expr(),
            {address_of_exprt{arg}},
            python_string_type(),
            get_location(expr)};
      }
    }
    return side_effect_expr_nondett{python_string_type(), get_location(expr)};
  }
  // PLib builtins: hash/id — return nondet int
  else if(func_name == "hash")
  {
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  // chr(n) → single-character string
  else if(func_name == "chr")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt code_point = convert_expression(*as_array(args).begin());
      // Range check: chr() requires 0 <= arg <= 0x10ffff
      add_check(
        and_exprt{
          binary_relation_exprt{
            code_point, ID_ge, from_integer(0, code_point.type())},
          binary_relation_exprt{
            code_point, ID_le, from_integer(0x10ffff, code_point.type())}},
        "value-error",
        "chr() arg not in range(0x110000)",
        get_location(expr));
      struct_typet str_type = python_string_struct_def();
      const auto &data_type = array_typet(
        unsignedbv_typet{8},
        from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));
      exprt::operandst chars;

      // For constant code points, encode as UTF-8
      mp_integer cp_val;
      // Try to resolve variable code points via try_eval_double
      auto cp_dbl = try_eval_double(code_point);
      if(
        (code_point.is_constant() &&
         !to_integer(to_constant_expr(code_point), cp_val)) ||
        (cp_dbl.has_value() &&
         (cp_val = static_cast<long long>(cp_dbl.value()), true)))
      {
        long cp = cp_val.to_long();
        if(cp < 0x80)
        {
          chars.push_back(from_integer(cp, unsignedbv_typet{8}));
        }
        else if(cp < 0x800)
        {
          chars.push_back(from_integer(0xC0 | (cp >> 6), unsignedbv_typet{8}));
          chars.push_back(
            from_integer(0x80 | (cp & 0x3F), unsignedbv_typet{8}));
        }
        else if(cp < 0x10000)
        {
          chars.push_back(from_integer(0xE0 | (cp >> 12), unsignedbv_typet{8}));
          chars.push_back(
            from_integer(0x80 | ((cp >> 6) & 0x3F), unsignedbv_typet{8}));
          chars.push_back(
            from_integer(0x80 | (cp & 0x3F), unsignedbv_typet{8}));
        }
        else
        {
          chars.push_back(from_integer(0xF0 | (cp >> 18), unsignedbv_typet{8}));
          chars.push_back(
            from_integer(0x80 | ((cp >> 12) & 0x3F), unsignedbv_typet{8}));
          chars.push_back(
            from_integer(0x80 | ((cp >> 6) & 0x3F), unsignedbv_typet{8}));
          chars.push_back(
            from_integer(0x80 | (cp & 0x3F), unsignedbv_typet{8}));
        }
      }
      else
      {
        // Non-constant: single byte (truncated)
        chars.push_back(safe_typecast(code_point, unsignedbv_typet{8}));
      }

      // For constant code points, use python_string_literal
      if(code_point.is_constant())
      {
        std::string s;
        for(const auto &c : chars)
        {
          mp_integer v;
          if(!to_integer(to_constant_expr(c), v))
            s += static_cast<char>(v.to_long());
        }
        return python_string_literal(s);
      }
      // Non-constant: build pointer-based string
      array_typet at(
        unsignedbv_typet{8},
        from_integer(static_cast<long long>(chars.size()), signedbv_typet{64}));
      array_exprt arr(std::move(chars), at);
      exprt ptr = address_of_exprt(index_exprt(
        arr, from_integer(0, signedbv_typet{64}), unsignedbv_typet{8}));
      exprt length = from_integer(1LL, signedbv_typet{64});
      return struct_exprt{{length, ptr}, str_type};
    }
    return side_effect_expr_nondett{python_string_type(), get_location(expr)};
  }
  // ord(c) → integer code point of single character
  else if(func_name == "ord")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(is_python_string_type(arg.type()))
      {
        // For constant strings, compute Unicode code point
        auto sv = extract_string_value(arg);
        if(sv.has_value() && !sv.value().empty())
        {
          const std::string &s = sv.value();
          unsigned char c0 = static_cast<unsigned char>(s[0]);
          long cp = c0;
          if(c0 >= 0xC0 && c0 < 0xE0 && s.size() >= 2)
            cp = ((c0 & 0x1F) << 6) | (static_cast<unsigned char>(s[1]) & 0x3F);
          else if(c0 >= 0xE0 && c0 < 0xF0 && s.size() >= 3)
            cp = ((c0 & 0x0F) << 12) |
                 ((static_cast<unsigned char>(s[1]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(s[2]) & 0x3F);
          else if(c0 >= 0xF0 && s.size() >= 4)
            cp = ((c0 & 0x07) << 18) |
                 ((static_cast<unsigned char>(s[1]) & 0x3F) << 12) |
                 ((static_cast<unsigned char>(s[2]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(s[3]) & 0x3F);
          return from_integer(cp, python_int_type());
        }
        // Symbolic: return first byte
        const auto &data_type = array_typet(
          unsignedbv_typet{8},
          from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));
        member_exprt data{arg, "data", data_type};
        return safe_typecast(
          index_exprt{data, from_integer(0, signedbv_typet{64})},
          python_int_type());
      }
    }
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  // complex(real, imag) — return struct with real/imag fields
  else if(func_name == "complex")
  {
    struct_typet::componentst comps;
    comps.push_back(struct_typet::componentt{"real", double_type()});
    comps.push_back(struct_typet::componentt{"imag", double_type()});
    struct_typet complex_type{comps};
    complex_type.set_tag("python_complex");

    exprt real_val = safe_zero(double_type());
    exprt imag_val = safe_zero(double_type());
    if(args.is_array())
    {
      auto it = as_array(args).begin();
      if(it != as_array(args).end())
      {
        exprt arg = convert_expression(*it);
        if(arg.type().id() == ID_floatbv)
          real_val = arg;
        else
        {
          ieee_floatt fv{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          if(arg.is_constant())
          {
            mp_integer iv;
            if(!to_integer(to_constant_expr(arg), iv))
              fv.from_integer(iv);
          }
          real_val = fv.to_expr();
        }
        ++it;
      }
      if(it != as_array(args).end())
      {
        exprt arg = convert_expression(*it);
        if(arg.type().id() == ID_floatbv)
          imag_val = arg;
        else
        {
          ieee_floatt fv{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          if(arg.is_constant())
          {
            mp_integer iv;
            if(!to_integer(to_constant_expr(arg), iv))
              fv.from_integer(iv);
          }
          imag_val = fv.to_expr();
        }
      }
    }
    return struct_exprt{{real_val, imag_val}, complex_type};
  }
  // PLib stdtypes: set(iterable) — deduplicate elements
  else if(func_name == "dict")
  {
    // dict(a=1, b=2) — construct from keyword arguments
    const jsont &keywords = json_member(expr, "keywords");
    if(keywords.is_array() && !as_array(keywords).empty())
    {
      typet dict_type =
        python_dict_type(python_string_type(), python_int_type());
      // Determine value type from first keyword
      exprt first_val =
        convert_expression(json_member(*as_array(keywords).begin(), "value"));
      dict_type = python_dict_type(python_string_type(), first_val.type());
      const auto &dict_st = to_struct_type(dict_type);
      const auto &keys_arr_type = to_array_type(dict_st.components()[1].type());
      const auto &vals_arr_type = to_array_type(dict_st.components()[2].type());

      exprt::operandst keys, vals;
      for(const auto &kw : as_array(keywords))
      {
        std::string kname = json_string(json_member(kw, "arg"));
        exprt kval = convert_expression(json_member(kw, "value"));
        keys.push_back(python_string_literal(kname));
        if(kval.type() != vals_arr_type.element_type())
          kval = safe_typecast(kval, vals_arr_type.element_type());
        vals.push_back(kval);
      }
      while(keys.size() < PYTHON_MAX_DICT_SIZE)
      {
        keys.push_back(safe_zero(keys_arr_type.element_type()));
        vals.push_back(safe_zero(vals_arr_type.element_type()));
      }
      return struct_exprt{
        {from_integer(
           static_cast<long long>(as_array(keywords).size()),
           python_int_type()),
         array_exprt{std::move(keys), keys_arr_type},
         array_exprt{std::move(vals), vals_arr_type}},
        dict_type};
    }
    // dict() with no args — empty dict
    if(!args.is_array() || as_array(args).empty())
    {
      typet dict_type =
        python_dict_type(python_string_type(), python_int_type());
      return safe_zero(dict_type);
    }
    return side_effect_expr_nondett{
      python_dict_type(python_string_type(), python_int_type()),
      get_location(expr)};
  }
  else if(func_name == "set")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(is_python_list_type(arg.type()))
      {
        const auto &list_st = to_struct_type(arg.type());
        const auto &data_type = to_array_type(list_st.components()[1].type());
        member_exprt src_data{arg, "data", data_type};
        member_exprt src_len{arg, "length", signedbv_typet{64}};

        // Create result list
        static unsigned set_counter = 0;
        std::string tmp_name = "__set_" + std::to_string(set_counter++);
        std::string tmp_qname = qualify_name(tmp_name);
        irep_idt tmp_id{tmp_qname};
        if(symbol_table.lookup(tmp_id) == nullptr)
        {
          symbolt tmp_sym{tmp_id, arg.type(), "python"};
          tmp_sym.base_name = tmp_name;
          tmp_sym.is_lvalue = true;
          tmp_sym.is_state_var = true;
          symbol_table.add(tmp_sym);
        }
        symbol_exprt tmp = symbol_table.lookup_ref(tmp_id).symbol_expr();
        member_exprt dst_data{tmp, "data", data_type};
        member_exprt dst_len{tmp, "length", signedbv_typet{64}};

        // Initialize result length to 0
        pending_checks.push_back(
          code_frontend_assignt{dst_len, from_integer(0, signedbv_typet{64})});

        // For each input element, check if already in result
        for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
        {
          exprt idx = from_integer(i, signedbv_typet{64});
          exprt in_bounds = binary_relation_exprt{idx, ID_lt, src_len};
          exprt elem = index_exprt{src_data, idx};

          // Check if elem is already in result[0..dst_len)
          // Build: found = result[0]==elem || result[1]==elem || ...
          static unsigned found_ctr = 0;
          std::string fn = "__set_found_" + std::to_string(found_ctr++);
          std::string fq = qualify_name(fn);
          irep_idt fi{fq};
          if(symbol_table.lookup(fi) == nullptr)
          {
            symbolt fs{fi, bool_typet{}, "python"};
            fs.base_name = fn;
            fs.is_lvalue = true;
            fs.is_state_var = true;
            symbol_table.add(fs);
          }
          symbol_exprt found = symbol_table.lookup_ref(fi).symbol_expr();
          pending_checks.push_back(code_frontend_assignt{found, false_exprt{}});

          for(std::size_t j = 0; j < PYTHON_MAX_LIST_LENGTH; j++)
          {
            exprt jdx = from_integer(j, signedbv_typet{64});
            exprt j_in_result = binary_relation_exprt{jdx, ID_lt, dst_len};
            exprt match = equal_exprt{index_exprt{dst_data, jdx}, elem};
            pending_checks.push_back(code_ifthenelset{
              and_exprt{j_in_result, match},
              code_frontend_assignt{found, true_exprt{}}});
          }

          // If not found and in bounds, add to result
          code_blockt add_block;
          add_block.add(
            code_frontend_assignt{index_exprt{dst_data, dst_len}, elem});
          add_block.add(code_frontend_assignt{
            dst_len, plus_exprt{dst_len, from_integer(1, signedbv_typet{64})}});
          pending_checks.push_back(code_ifthenelset{
            and_exprt{in_bounds, not_exprt{found}}, std::move(add_block)});
        }

        return std::move(tmp);
      }
    }
    return side_effect_expr_nondett{
      python_list_type(python_int_type()), get_location(expr)};
  }
  // list() / reversed() — return copy or nondet
  else if(func_name == "list" || func_name == "reversed")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(is_python_list_type(arg.type()))
        return arg;
    }
    return side_effect_expr_nondett{
      python_list_type(python_int_type()), get_location(expr)};
  }
  // enumerate(iterable) → list of (index, element) tuples
  else if(func_name == "enumerate")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(is_python_list_type(arg.type()))
      {
        const auto &list_st = to_struct_type(arg.type());
        const auto &data_type = to_array_type(list_st.components()[1].type());
        typet elem_type = data_type.element_type();
        member_exprt length{arg, "length", signedbv_typet{64}};
        member_exprt data{arg, "data", data_type};
        struct_typet::componentst comps;
        comps.push_back(struct_typet::componentt{"_0", python_int_type()});
        comps.push_back(struct_typet::componentt{"_1", elem_type});
        struct_typet tuple_type{comps};
        tuple_type.set_tag("python_tuple");
        struct_typet result_list_type = python_list_type(tuple_type);
        const auto &result_data_type =
          to_array_type(result_list_type.components()[1].type());
        exprt::operandst elems;
        for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
        {
          exprt idx = from_integer(i, signedbv_typet{64});
          elems.push_back(struct_exprt{
            {from_integer(i, python_int_type()), index_exprt{data, idx}},
            tuple_type});
        }
        return struct_exprt{
          {length, array_exprt{std::move(elems), result_data_type}},
          result_list_type};
      }
    }
    return side_effect_expr_nondett{
      python_list_type(python_int_type()), get_location(expr)};
  }
  // PLib builtins: sorted(iterable) — return sorted copy
  else if(func_name == "sorted")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      // PLR builtins: sorted(iterable, /, *, key=None, reverse=False).
      // Pick up the reverse=... keyword; key= is not yet supported.
      bool sorted_reverse = false;
      // key=lambda is not yet modelled — the lambda body
      // would need per-element evaluation. We accept the
      // argument silently but ignore it. The caller gets
      // a list with the same elements as the input but in
      // the default ordering (which may not match Python
      // semantics when key= is supplied — documented as
      // limitation).
      const jsont &sorted_kw = json_member(expr, "keywords");
      if(sorted_kw.is_array())
      {
        for(const auto &k : as_array(sorted_kw))
        {
          std::string kn = json_string(json_member(k, "arg"));
          if(kn == "reverse")
          {
            exprt kv = convert_expression(json_member(k, "value"));
            if(kv.is_true())
              sorted_reverse = true;
          }
        }
      }
      if(is_python_list_type(arg.type()))
      {
        // Parse-time fast path: literal or tracked list sorted
        // in C++; returns a pre-sorted list_exprt.
        const exprt *lit = nullptr;
        if(arg.id() == ID_struct)
          lit = &arg;
        else if(arg.id() == ID_symbol)
        {
          auto it = list_literals.find(to_symbol_expr(arg).get_identifier());
          if(it != list_literals.end())
            lit = &it->second;
        }
        if(
          lit != nullptr && lit->operands().size() >= 2 &&
          lit->operands()[0].is_constant())
        {
          mp_integer lv;
          if(!to_integer(to_constant_expr(lit->operands()[0]), lv))
          {
            const exprt &data_arr = lit->operands()[1];
            std::vector<std::pair<mp_integer, exprt>> pairs;
            bool all_const = true;
            for(mp_integer i = 0; i < lv; ++i)
            {
              auto idx = i.to_ulong();
              if(idx >= data_arr.operands().size())
              {
                all_const = false;
                break;
              }
              const exprt &e = data_arr.operands()[idx];
              if(!e.is_constant() || e.type().id() != ID_signedbv)
              {
                all_const = false;
                break;
              }
              mp_integer val;
              if(to_integer(to_constant_expr(e), val))
              {
                all_const = false;
                break;
              }
              pairs.emplace_back(val, e);
            }
            if(all_const)
            {
              if(sorted_reverse)
                std::sort(
                  pairs.begin(),
                  pairs.end(),
                  [](const auto &a, const auto &b)
                  { return a.first > b.first; });
              else
                std::sort(
                  pairs.begin(),
                  pairs.end(),
                  [](const auto &a, const auto &b)
                  { return a.first < b.first; });
              const auto &list_st = to_struct_type(arg.type());
              const auto &data_type =
                to_array_type(list_st.components()[1].type());
              exprt::operandst sorted_elems;
              for(const auto &p : pairs)
                sorted_elems.push_back(p.second);
              while(sorted_elems.size() < PYTHON_MAX_LIST_LENGTH)
                sorted_elems.push_back(safe_zero(data_type.element_type()));
              return struct_exprt{
                {lit->operands()[0],
                 array_exprt{std::move(sorted_elems), data_type}},
                arg.type()};
            }
          }
        }
        // Create a copy and sort it
        static unsigned sorted_counter = 0;
        std::string tmp_name = "__sorted_" + std::to_string(sorted_counter++);
        std::string tmp_qname = qualify_name(tmp_name);
        irep_idt tmp_id{tmp_qname};
        if(symbol_table.lookup(tmp_id) == nullptr)
        {
          symbolt tmp_sym{tmp_id, arg.type(), "python"};
          tmp_sym.base_name = tmp_name;
          tmp_sym.is_lvalue = true;
          tmp_sym.is_state_var = true;
          symbol_table.add(tmp_sym);
        }
        symbol_exprt tmp = symbol_table.lookup_ref(tmp_id).symbol_expr();
        pending_checks.push_back(code_frontend_assignt{tmp, arg});
        // Bubble sort the copy
        const auto &list_st = to_struct_type(arg.type());
        const auto &data_type = to_array_type(list_st.components()[1].type());
        member_exprt data{tmp, "data", data_type};
        member_exprt length{tmp, "length", signedbv_typet{64}};
        for(std::size_t pass = 0; pass < PYTHON_MAX_LIST_LENGTH; pass++)
        {
          for(std::size_t i = 0; i + 1 < PYTHON_MAX_LIST_LENGTH; i++)
          {
            exprt idx = from_integer(i, signedbv_typet{64});
            exprt next = from_integer(i + 1, signedbv_typet{64});
            exprt guard = and_exprt{
              binary_relation_exprt{next, ID_lt, length},
              binary_relation_exprt{
                index_exprt{data, idx},
                sorted_reverse ? ID_lt : ID_gt,
                index_exprt{data, next}}};
            static unsigned stmp = 0;
            std::string sn = "__stmp_" + std::to_string(stmp++);
            std::string sq = qualify_name(sn);
            irep_idt si{sq};
            if(symbol_table.lookup(si) == nullptr)
            {
              symbolt ss{si, data_type.element_type(), "python"};
              ss.base_name = sn;
              ss.is_lvalue = true;
              ss.is_state_var = true;
              symbol_table.add(ss);
            }
            symbol_exprt sv = symbol_table.lookup_ref(si).symbol_expr();
            code_blockt swap;
            swap.add(code_frontend_assignt{sv, index_exprt{data, idx}});
            swap.add(code_frontend_assignt{
              index_exprt{data, idx}, index_exprt{data, next}});
            swap.add(code_frontend_assignt{index_exprt{data, next}, sv});
            pending_checks.push_back(code_ifthenelset{guard, std::move(swap)});
          }
        }
        return std::move(tmp);
      }
    }
    return side_effect_expr_nondett{
      python_list_type(python_int_type()), get_location(expr)};
  }
  // PLib builtins: sum(iterable) — sum of elements
  else if(func_name == "sum")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(!arg.is_nil() && is_python_list_type(arg.type()))
      {
        const auto &list_st = to_struct_type(arg.type());
        const auto &data_type = to_array_type(list_st.components()[1].type());
        member_exprt length{arg, "length", signedbv_typet{64}};
        member_exprt data{arg, "data", data_type};

        // Unrolled accumulation: result = sum of data[0..length-1]
        static unsigned sum_counter = 0;
        std::string tmp_name = "__sum_" + std::to_string(sum_counter++);
        std::string tmp_qname = qualify_name(tmp_name);
        irep_idt tmp_id{tmp_qname};
        if(symbol_table.lookup(tmp_id) == nullptr)
        {
          symbolt tmp_sym{tmp_id, data_type.element_type(), "python"};
          tmp_sym.base_name = tmp_name;
          tmp_sym.is_lvalue = true;
          tmp_sym.is_state_var = true;
          symbol_table.add(tmp_sym);
        }
        symbol_exprt tmp = symbol_table.lookup_ref(tmp_id).symbol_expr();
        pending_checks.push_back(code_frontend_assignt{
          tmp, from_integer(0, data_type.element_type())});
        for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
        {
          exprt idx = from_integer(i, signedbv_typet{64});
          pending_checks.push_back(code_ifthenelset{
            binary_relation_exprt{idx, ID_lt, length},
            code_frontend_assignt{
              tmp, plus_exprt{tmp, index_exprt{data, idx}}}});
        }
        return std::move(tmp);
      }
    }
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  // PLib builtins: range() as expression → list
  else if(func_name == "range")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt start, stop, step;
      auto it = as_array(args).begin();
      if(as_array(args).size() == 1)
      {
        start = from_integer(0, python_int_type());
        stop = convert_expression(*it);
        step = from_integer(1, python_int_type());
      }
      else
      {
        start = convert_expression(*it);
        ++it;
        stop = convert_expression(*it);
        step = from_integer(1, python_int_type());
        if(as_array(args).size() >= 3)
        {
          ++it;
          step = convert_expression(*it);
        }
      }
      start = safe_typecast(start, python_int_type());
      stop = safe_typecast(stop, python_int_type());
      step = safe_typecast(step, python_int_type());

      typet lt = python_list_type(python_int_type());
      static unsigned range_ctr = 0;
      std::string tn = "__range_" + std::to_string(range_ctr++);
      std::string tq = qualify_name(tn);
      irep_idt ti{tq};
      if(symbol_table.lookup(ti) == nullptr)
      {
        symbolt ts{ti, lt, "python"};
        ts.base_name = tn;
        ts.is_lvalue = true;
        ts.is_state_var = true;
        symbol_table.add(ts);
      }
      symbol_exprt tmp = symbol_table.lookup_ref(ti).symbol_expr();
      const auto &data_type =
        to_array_type(to_struct_type(lt).components()[1].type());
      member_exprt data{tmp, "data", data_type};
      member_exprt length{tmp, "length", signedbv_typet{64}};

      // Fill: data[i] = start + i * step for i in 0..MAX
      pending_checks.push_back(
        code_frontend_assignt{length, from_integer(0, signedbv_typet{64})});
      for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
      {
        exprt idx = from_integer(i, signedbv_typet{64});
        exprt val = plus_exprt{start, mult_exprt{idx, step}};
        exprt in_range = binary_relation_exprt{val, ID_lt, stop};
        code_blockt add;
        add.add(code_frontend_assignt{index_exprt{data, idx}, val});
        add.add(code_frontend_assignt{
          length, plus_exprt{length, from_integer(1, signedbv_typet{64})}});
        pending_checks.push_back(code_ifthenelset{in_range, std::move(add)});
      }
      return std::move(tmp);
    }
    return side_effect_expr_nondett{
      python_list_type(python_int_type()), get_location(expr)};
  }
  // PLib builtins: round(number) → nearest integer
  else if(func_name == "round")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      auto ait = as_array(args).begin();
      exprt arg = convert_expression(*ait);
      auto eval_val = try_eval_double(arg);
      if(eval_val.has_value())
      {
        double val = eval_val.value();
        int ndigits = 0;
        ++ait;
        if(ait != as_array(args).end())
        {
          exprt nd = convert_expression(*ait);
          auto nev = try_eval_double(nd);
          if(nev.has_value())
            ndigits = static_cast<int>(nev.value());
        }
        double factor = std::pow(10.0, ndigits);
        double rounded = std::round(val * factor) / factor;
        if(ndigits > 0)
          return double_to_floatbv(rounded);
        return from_integer(static_cast<long long>(rounded), python_int_type());
      }
    }
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  // PLib builtins: divmod(a, b) returns (a // b, a % b)
  else if(func_name == "divmod")
  {
    if(args.is_array() && as_array(args).size() >= 2)
    {
      auto it = as_array(args).begin();
      exprt a = convert_expression(*it);
      ++it;
      exprt b = convert_expression(*it);
      a = safe_typecast(a, python_int_type());
      b = safe_typecast(b, python_int_type());
      // Use float type if either arg is float
      typet result_type = python_int_type();
      if(a.type().id() == ID_floatbv || b.type().id() == ID_floatbv)
      {
        result_type = double_type();
        a = safe_typecast(a, double_type());
        b = safe_typecast(b, double_type());
      }
      struct_typet::componentst comps;
      comps.push_back(struct_typet::componentt{"_0", result_type});
      comps.push_back(struct_typet::componentt{"_1", result_type});
      struct_typet tuple_type{comps};
      tuple_type.set_tag("python_tuple");
      // PLR §6.7: divmod uses floor division and Python modulo
      if(result_type.id() == ID_floatbv)
      {
        // Float divmod: quotient = floor(a/b), remainder = a - quotient*b
        // Use typecast to int for floor
        exprt q_raw = div_exprt{a, b};
        exprt q_int = typecast_exprt{q_raw, python_int_type()};
        exprt q_float = typecast_exprt{q_int, double_type()};
        // Adjust for negative: if q_raw < q_float, subtract 1
        exprt floor_q = minus_exprt{
          q_float,
          if_exprt{
            binary_relation_exprt{q_float, ID_gt, q_raw},
            typecast_exprt{from_integer(1, python_int_type()), double_type()},
            typecast_exprt{from_integer(0, python_int_type()), double_type()}}};
        exprt py_mod = minus_exprt{a, mult_exprt{floor_q, b}};
        return struct_exprt{{floor_q, py_mod}, tuple_type};
      }
      exprt quotient = div_exprt{a, b};
      exprt remainder = mod_exprt{a, b};
      exprt has_remainder =
        notequal_exprt{remainder, from_integer(0, a.type())};
      exprt diff_sign = binary_relation_exprt{
        bitxor_exprt{a, b}, ID_lt, from_integer(0, a.type())};
      exprt floor_q = minus_exprt{
        quotient,
        if_exprt{
          and_exprt{has_remainder, diff_sign},
          from_integer(1, a.type()),
          from_integer(0, a.type())}};
      exprt py_mod = if_exprt{
        and_exprt{has_remainder, diff_sign},
        plus_exprt{remainder, b},
        remainder};
      return struct_exprt{{floor_q, py_mod}, tuple_type};
    }
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  // str() — return nondet string
  else if(func_name == "str")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      // str(string) — return as-is
      if(is_python_string_type(arg.type()))
        return arg;
      // str(bool) — "True" or "False"
      if(arg.type().id() == ID_bool || arg.type().id() == ID_c_bool)
      {
        if(arg.is_true())
          return python_string_literal("True");
        if(arg.is_false())
          return python_string_literal("False");
      }
      // str(class_instance) — call __str__ if available
      if(arg.type().id() == ID_struct)
      {
        const auto &tag = to_struct_type(arg.type()).get_tag();
        if(id2string(tag).substr(0, 13) == "python_class_")
        {
          std::string cls = id2string(tag).substr(13); // strip "python_class_"
          irep_idt str_id{"python::" + cls + "::__str__"};
          const symbolt *str_sym = symbol_table.lookup(str_id);
          if(str_sym != nullptr)
          {
            exprt self_ptr = address_of_exprt{arg};
            side_effect_expr_function_callt call{
              str_sym->symbol_expr(),
              {self_ptr},
              python_string_type(),
              get_location(expr)};
            return std::move(call);
          }
        }
      }
      // str(int/float) — convert at conversion time using try_eval_double
      {
        auto ev = try_eval_double(arg);
        if(ev.has_value())
        {
          double d = ev.value();
          std::string s;
          if(
            d == std::floor(d) && std::abs(d) < 1e15 &&
            (arg.type().id() == ID_signedbv || arg.type().id() == ID_integer ||
             (arg.id() == ID_symbol && arg.type().id() != ID_floatbv)))
            s = std::to_string(static_cast<long long>(d));
          else
          {
            std::ostringstream oss;
            oss << d;
            s = oss.str();
            // Python-style: remove trailing zeros after decimal
            if(s.find('.') != std::string::npos)
            {
              while(s.size() > 1 && s.back() == '0' && s[s.size() - 2] != '.')
                s.pop_back();
            }
          }
          return python_string_literal(s);
        }
      }
      // str(int_constant) — legacy path
      if(arg.is_constant() && arg.type().id() == ID_signedbv)
      {
        mp_integer iv;
        if(!to_integer(to_constant_expr(arg), iv))
        {
          std::string s = integer2string(iv);
          return python_string_literal(s);
        }
      }
      // str(float_constant)
      if(arg.is_constant() && arg.type().id() == ID_floatbv)
      {
        ieee_floatt fv{
          ieee_float_spect::double_precision(),
          ieee_floatt::rounding_modet::ROUND_TO_EVEN};
        fv.from_expr(to_constant_expr(arg));
        std::string s = fv.to_ansi_c_string();
        if(s.find(".") != std::string::npos)
        {
          while(s.size() > 1 && s.back() == '0' && s[s.size() - 2] != '.')
            s.pop_back();
        }
        return python_string_literal(s);
      }
      // Symbolic int: emit cprover_string_of_int_func so the
      // solver knows the result's content and length
      // precisely.
      if(arg.type().id() == ID_signedbv || arg.type().id() == ID_integer)
      {
        exprt as_i64 = arg.type() == signedbv_typet{64}
                         ? arg
                         : safe_typecast(arg, signedbv_typet{64});
        exprt result = emit_string_function(
          ID_cprover_string_of_int_func,
          {as_i64},
          symbol_table,
          pending_checks);
        auto ensure_fn = [&](const irep_idt &fid)
        {
          if(symbol_table.lookup(fid) == nullptr)
          {
            array_typet inf_array_type{
              unsignedbv_typet{8}, infinity_exprt(signedbv_typet{64})};
            std::vector<typet> at;
            if(fid == ID_cprover_associate_array_to_pointer_func)
            {
              at.push_back(inf_array_type);
              at.push_back(pointer_typet(unsignedbv_typet{8}, 64));
            }
            else
            {
              at.push_back(inf_array_type);
              at.push_back(signedbv_typet{64});
            }
            symbolt fs{
              fid,
              mathematical_function_typet(std::move(at), signedbv_typet{32}),
              "python"};
            fs.base_name = id2string(fid);
            symbol_table.add(fs);
          }
        };
        ensure_fn(ID_cprover_associate_array_to_pointer_func);
        ensure_fn(ID_cprover_associate_length_to_array_func);
        return result;
      }
      // Symbolic float: emit cprover_string_of_double_func
      // (Python floats are double-precision) so the solver
      // knows the result's content precisely.
      if(arg.type().id() == ID_floatbv)
      {
        exprt result = emit_string_function(
          ID_cprover_string_of_double_func,
          {arg},
          symbol_table,
          pending_checks);
        auto ensure_fn2 = [&](const irep_idt &fid)
        {
          if(symbol_table.lookup(fid) == nullptr)
          {
            array_typet inf_array_type{
              unsignedbv_typet{8}, infinity_exprt(signedbv_typet{64})};
            std::vector<typet> at;
            if(fid == ID_cprover_associate_array_to_pointer_func)
            {
              at.push_back(inf_array_type);
              at.push_back(pointer_typet(unsignedbv_typet{8}, 64));
            }
            else
            {
              at.push_back(inf_array_type);
              at.push_back(signedbv_typet{64});
            }
            symbolt fs{
              fid,
              mathematical_function_typet(std::move(at), signedbv_typet{32}),
              "python"};
            fs.base_name = id2string(fid);
            symbol_table.add(fs);
          }
        };
        ensure_fn2(ID_cprover_associate_array_to_pointer_func);
        ensure_fn2(ID_cprover_associate_length_to_array_func);
        return result;
      }
      return side_effect_expr_nondett{python_string_type(), get_location(expr)};
    }
    // str() with no arguments → empty string
    return python_string_literal("");
  }
  // all(genexp) / any(genexp) — unroll for literal iterables
  else if(func_name == "all" || func_name == "any")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      const jsont &arg = *as_array(args).begin();
      if(is_node_type(arg, "GeneratorExp"))
      {
        const jsont &elt = json_member(arg, "elt");
        const jsont &generators = json_member(arg, "generators");
        if(generators.is_array() && !as_array(generators).empty())
        {
          const jsont &gen = *as_array(generators).begin();
          const jsont &gen_iter = json_member(gen, "iter");
          const jsont &gen_target = json_member(gen, "target");
          std::string iter_var = json_string(json_member(gen_target, "id"));

          if(is_node_type(gen_iter, "List") || is_node_type(gen_iter, "Name"))
          {
            // Get the iterable elements
            exprt iterable = convert_expression(gen_iter);
            const jsont *elts_json = nullptr;
            if(is_node_type(gen_iter, "List"))
              elts_json = &json_member(gen_iter, "elts");

            if(elts_json != nullptr && elts_json->is_array())
            {
              // Literal list: unroll
              std::string qname = qualify_name(iter_var);
              irep_idt iter_sym_id{qname};
              if(symbol_table.lookup(iter_sym_id) == nullptr)
              {
                symbolt sym{iter_sym_id, python_int_type(), "python"};
                sym.base_name = iter_var;
                sym.is_lvalue = true;
                sym.is_state_var = true;
                symbol_table.add(sym);
              }

              exprt result = (func_name == "all") ? exprt{true_exprt{}}
                                                  : exprt{false_exprt{}};

              for(const auto &val_json : as_array(*elts_json))
              {
                exprt val = convert_expression(val_json);
                exprt elt_expr = convert_expression(elt);
                // Substitute iter_var with concrete value
                std::function<void(exprt &)> subst = [&](exprt &e)
                {
                  if(
                    e.id() == ID_symbol &&
                    to_symbol_expr(e).get_identifier() == iter_sym_id)
                    e = val;
                  else
                    for(auto &op : e.operands())
                      subst(op);
                };
                subst(elt_expr);

                if(elt_expr.type() != bool_typet{})
                  elt_expr = typecast_exprt{elt_expr, bool_typet{}};

                if(func_name == "all")
                  result = and_exprt{result, elt_expr};
                else
                  result = or_exprt{result, elt_expr};
              }
              return result;
            }

            // Variable iterable: iterate over list data array
            if(!iterable.is_nil() && is_python_list_type(iterable.type()))
            {
              const auto &list_st = to_struct_type(iterable.type());
              const auto &data_type =
                to_array_type(list_st.components()[1].type());
              member_exprt data{iterable, "data", data_type};
              member_exprt length{iterable, "length", signedbv_typet{64}};

              std::string qname = qualify_name(iter_var);
              irep_idt iter_sym_id{qname};
              if(symbol_table.lookup(iter_sym_id) == nullptr)
              {
                symbolt sym{iter_sym_id, data_type.element_type(), "python"};
                sym.base_name = iter_var;
                sym.is_lvalue = true;
                sym.is_state_var = true;
                symbol_table.add(sym);
              }

              exprt result = (func_name == "all") ? exprt{true_exprt{}}
                                                  : exprt{false_exprt{}};
              for(std::size_t i = 0;
                  i < std::min(
                        static_cast<std::size_t>(16),
                        static_cast<std::size_t>(PYTHON_MAX_LIST_LENGTH));
                  i++)
              {
                exprt idx = from_integer(i, signedbv_typet{64});
                exprt in_range = binary_relation_exprt{idx, ID_lt, length};
                exprt elem = index_exprt{data, idx};

                exprt elt_expr = convert_expression(elt);
                std::function<void(exprt &)> subst = [&](exprt &e)
                {
                  if(
                    e.id() == ID_symbol &&
                    to_symbol_expr(e).get_identifier() == iter_sym_id)
                    e = elem;
                  else
                    for(auto &op : e.operands())
                      subst(op);
                };
                subst(elt_expr);

                if(elt_expr.type() != bool_typet{})
                  elt_expr = safe_typecast(elt_expr, bool_typet{});

                if(func_name == "all")
                  result =
                    and_exprt{result, or_exprt{not_exprt{in_range}, elt_expr}};
                else
                  result = or_exprt{result, and_exprt{in_range, elt_expr}};
              }
              return result;
            }
          }
        }
      }
      // Non-generator argument: all([x, y]) / any([x, y])
      exprt arg_expr = convert_expression(arg);
      if(!arg_expr.is_nil() && is_python_list_type(arg_expr.type()))
      {
        // Iterate over list elements
        const auto &list_st = to_struct_type(arg_expr.type());
        const auto &data_type = to_array_type(list_st.components()[1].type());
        member_exprt data{arg_expr, "data", data_type};
        member_exprt length{arg_expr, "length", signedbv_typet{64}};

        exprt result =
          (func_name == "all") ? exprt{true_exprt{}} : exprt{false_exprt{}};
        for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
        {
          exprt idx = from_integer(i, signedbv_typet{64});
          exprt in_range = binary_relation_exprt{idx, ID_lt, length};
          exprt elem = index_exprt{data, idx};
          exprt truthy = safe_typecast(elem, bool_typet{});

          if(func_name == "all")
            result = and_exprt{result, or_exprt{not_exprt{in_range}, truthy}};
          else
            result = or_exprt{result, and_exprt{in_range, truthy}};
        }
        return result;
      }
      if(!arg_expr.is_nil())
        return side_effect_expr_nondett{bool_typet{}, get_location(expr)};
    }
    return side_effect_expr_nondett{bool_typet{}, get_location(expr)};
  }
  // PLib builtins: type(obj) — return the type of an object
  // We model this as a static type tag for comparison with type names.
  else if(func_name == "hasattr" || func_name == "callable")
  {
    return side_effect_expr_nondett{bool_typet{}, get_location(expr)};
  }
  else if(
    func_name == "TypeVar" || func_name == "NewType" ||
    func_name == "overload" || func_name == "dataclass" || func_name == "field")
  {
    // typing/dataclass decorators and constructors — return nondet
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  else if(func_name == "type")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(!arg.is_nil())
      {
        // Return a type-tag constant based on static type
        int tag = 0; // unknown
        if(arg.type().id() == ID_signedbv || arg.type().id() == ID_integer)
          tag = 1;
        else if(arg.type().id() == ID_floatbv)
          tag = 2;
        else if(arg.type().id() == ID_bool)
          tag = 3;
        else if(is_python_string_type(arg.type()))
          tag = 4;
        else if(is_python_list_type(arg.type()))
          tag = 5;
        else if(is_python_tuple_type(arg.type()))
          tag = 6;
        else if(is_python_dict_type(arg.type()))
          tag = 7;
        return from_integer(tag, python_int_type());
      }
    }
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  // PLR §6.10.2: isinstance(obj, classinfo)
  // "Return True if the object argument is an instance of the classinfo
  // argument, or of a (direct, indirect, or virtual) subclass thereof."
  else if(func_name == "isinstance")
  {
    if(args.is_array() && as_array(args).size() >= 2)
    {
      auto it = as_array(args).begin();
      exprt obj = convert_expression(*it);
      ++it;
      std::string cls_name;
      if(is_node_type(*it, "Name"))
        cls_name = json_string(json_member(*it, "id"));

      // PLR §6.10.2: isinstance(x, (A, B)) — tuple of types
      if(is_node_type(*it, "Tuple"))
      {
        const jsont &elts = json_member(*it, "elts");
        if(elts.is_array() && !obj.is_nil())
        {
          // Tagged union: OR the per-tag checks.
          if(is_python_value_type(obj.type()))
          {
            exprt any_match = false_exprt{};
            for(const auto &elt : as_array(elts))
            {
              if(!is_node_type(elt, "Name"))
                continue;
              std::string tname = json_string(json_member(elt, "id"));
              exprt m;
              if(tname == "int")
                m = python_value_is(obj, python_type_tagt::INT);
              else if(tname == "float")
                m = python_value_is(obj, python_type_tagt::FLOAT);
              else if(tname == "bool")
                m = python_value_is(obj, python_type_tagt::BOOL);
              else if(tname == "str")
                m = python_value_is(obj, python_type_tagt::STR);
              else if(tname == "list")
                m = python_value_is(obj, python_type_tagt::LIST);
              else if(class_types.count(tname) > 0)
              {
                // Precise per-class dispatch on __class_tag.
                // Same logic as the single-name path below.
                std::set<std::string> matching;
                matching.insert(tname);
                bool grew = true;
                while(grew)
                {
                  grew = false;
                  for(const auto &[cand, cand_bases] : class_bases)
                  {
                    if(matching.count(cand) > 0)
                      continue;
                    for(const auto &b : cand_bases)
                    {
                      if(matching.count(b) > 0)
                      {
                        matching.insert(cand);
                        grew = true;
                        break;
                      }
                    }
                  }
                }
                exprt class_ptr = python_value_class_ptr(obj);
                pointer_typet i32_ptr{signedbv_typet{32}, 64};
                dereference_exprt class_tag{
                  typecast_exprt{class_ptr, i32_ptr}, signedbv_typet{32}};
                exprt tag_check = false_exprt{};
                for(const auto &mm : matching)
                {
                  auto ti = class_tag_ids.find(mm);
                  if(ti == class_tag_ids.end())
                    continue;
                  exprt eq = equal_exprt{
                    class_tag, from_integer(ti->second, signedbv_typet{32})};
                  if(tag_check.id() == ID_false)
                    tag_check = std::move(eq);
                  else
                    tag_check = or_exprt{std::move(tag_check), std::move(eq)};
                }
                m = and_exprt{
                  python_value_is(obj, python_type_tagt::CLASS),
                  std::move(tag_check)};
              }
              else
                continue;
              any_match = or_exprt{any_match, m};
            }
            return any_match;
          }
          exprt result = false_exprt{};
          for(const auto &elt : as_array(elts))
          {
            if(is_node_type(elt, "Name"))
            {
              std::string tname = json_string(json_member(elt, "id"));
              bool match = false;
              if(
                tname == "int" && (obj.type().id() == ID_signedbv ||
                                   obj.type().id() == ID_integer))
                match = true;
              else if(tname == "float" && obj.type().id() == ID_floatbv)
                match = true;
              else if(tname == "bool" && obj.type().id() == ID_bool)
                match = true;
              else if(tname == "str" && is_python_string_type(obj.type()))
                match = true;
              else if(tname == "list" && is_python_list_type(obj.type()))
                match = true;
              else if(tname == "set" && is_python_set_type(obj.type()))
                match = true;
              else if(tname == "dict" && is_python_dict_type(obj.type()))
                match = true;
              else if(
                tname == "complex" && obj.type().id() == ID_struct &&
                to_struct_type(obj.type()).get_tag() == "python_complex")
                match = true;
              if(match)
                return true_exprt{};
            }
          }
          return false_exprt{};
        }
      }

      // Handle isinstance(x, type(None)) — check for NoneType
      if(
        !obj.is_nil() && is_node_type(*it, "Call") &&
        is_node_type(json_member(*it, "func"), "Name") &&
        json_string(json_member(json_member(*it, "func"), "id")) == "type")
      {
        const jsont &type_args = json_member(*it, "args");
        if(
          type_args.is_array() && !as_array(type_args).empty() &&
          is_node_type(*as_array(type_args).begin(), "Constant") &&
          json_member(*as_array(type_args).begin(), "value").is_null())
        {
          // isinstance(x, type(None)) — check if x is None
          if(is_python_value_type(obj.type()))
            return python_value_is(obj, python_type_tagt::NONE);
          if(obj.type().id() == ID_signedbv)
          {
            // None sentinel check
            return equal_exprt{
              obj,
              from_integer(mp_integer{-4611686018427387904LL}, obj.type())};
          }
          return false_exprt{};
        }
      }

      if(!obj.is_nil() && !cls_name.empty())
      {
        // Tagged union: isinstance checks the tag field
        if(is_python_value_type(obj.type()))
        {
          if(cls_name == "int")
            return python_value_is(obj, python_type_tagt::INT);
          if(cls_name == "float")
            return python_value_is(obj, python_type_tagt::FLOAT);
          if(cls_name == "bool")
            return python_value_is(obj, python_type_tagt::BOOL);
          if(cls_name == "str")
            return python_value_is(obj, python_type_tagt::STR);
          if(cls_name == "list")
            return python_value_is(obj, python_type_tagt::LIST);
          // User-defined class: dispatch precisely on the
          // __class_tag read through __class_ptr.
          //
          // The pointer is typed as opaque (void*); we cast
          // it to pointer-to-int32 to read __class_tag, which
          // sits at offset 0 in every class struct. Then we
          // OR-compare against the tag of cls_name plus the
          // tags of all classes that inherit from cls_name
          // (forward BFS through class_bases to find
          // subclasses).
          if(class_types.count(cls_name) > 0)
          {
            // Collect cls_name + all known subclasses of cls_name
            // (any class whose ancestor chain includes cls_name).
            std::set<std::string> matching;
            matching.insert(cls_name);
            bool grew = true;
            while(grew)
            {
              grew = false;
              for(const auto &[cand, cand_bases] : class_bases)
              {
                if(matching.count(cand) > 0)
                  continue;
                for(const auto &b : cand_bases)
                {
                  if(matching.count(b) > 0)
                  {
                    matching.insert(cand);
                    grew = true;
                    break;
                  }
                }
              }
            }
            // Build OR of class_tag equality checks.
            exprt class_ptr = python_value_class_ptr(obj);
            pointer_typet i32_ptr{signedbv_typet{32}, 64};
            dereference_exprt class_tag{
              typecast_exprt{class_ptr, i32_ptr}, signedbv_typet{32}};
            exprt tag_check = false_exprt{};
            for(const auto &m : matching)
            {
              auto ti = class_tag_ids.find(m);
              if(ti == class_tag_ids.end())
                continue;
              exprt eq = equal_exprt{
                class_tag, from_integer(ti->second, signedbv_typet{32})};
              if(tag_check.id() == ID_false)
                tag_check = std::move(eq);
              else
                tag_check = or_exprt{std::move(tag_check), std::move(eq)};
            }
            // Guard with the outer CLASS tag check — the precise
            // dispatch only applies when tag == CLASS.
            return and_exprt{
              python_value_is(obj, python_type_tagt::CLASS),
              std::move(tag_check)};
          }
          return false_exprt{}; // not a known type
        }

        // Check built-in types first
        if(
          cls_name == "int" &&
          (obj.type().id() == ID_signedbv || obj.type().id() == ID_integer))
          return true_exprt{};
        if(cls_name == "float" && obj.type().id() == ID_floatbv)
          return true_exprt{};
        if(cls_name == "bool" && obj.type().id() == ID_bool)
          return true_exprt{};
        if(cls_name == "str" && is_python_string_type(obj.type()))
          return true_exprt{};
        if(cls_name == "list" && is_python_list_type(obj.type()))
          return true_exprt{};
        if(cls_name == "tuple" && is_python_tuple_type(obj.type()))
          return true_exprt{};
        if(cls_name == "dict" && is_python_dict_type(obj.type()))
          return true_exprt{};
        if(cls_name == "set" && is_python_set_type(obj.type()))
          return true_exprt{};
        if(
          cls_name == "complex" && obj.type().id() == ID_struct &&
          to_struct_type(obj.type()).get_tag() == "python_complex")
          return true_exprt{};

        // If checking against a built-in type and obj is a different
        // built-in type, return false (no cross-type isinstance)
        if(
          cls_name == "int" || cls_name == "float" || cls_name == "bool" ||
          cls_name == "str" || cls_name == "list" || cls_name == "tuple" ||
          cls_name == "dict")
        {
          // obj is not the requested built-in type
          if(
            obj.type().id() == ID_signedbv || obj.type().id() == ID_integer ||
            obj.type().id() == ID_floatbv || obj.type().id() == ID_bool ||
            is_python_string_type(obj.type()) ||
            is_python_list_type(obj.type()) ||
            is_python_tuple_type(obj.type()) || is_python_dict_type(obj.type()))
            return false_exprt{};
        }

        // Check user-defined classes
        if(obj.type().id() == ID_struct)
        {
          const auto &st = to_struct_type(obj.type());
          std::string tag = id2string(st.get_tag());
          std::string obj_class;
          if(tag.substr(0, 13) == "python_class_")
            obj_class = tag.substr(13);

          if(!obj_class.empty())
          {
            // BFS through inheritance hierarchy (supports multiple inheritance)
            std::vector<std::string> queue = {obj_class};
            std::set<std::string> visited;
            while(!queue.empty())
            {
              std::string check = queue.back();
              queue.pop_back();
              if(visited.count(check))
                continue;
              visited.insert(check);
              if(check == cls_name)
                return true_exprt{};
              auto base_it = class_bases.find(check);
              if(base_it != class_bases.end())
              {
                for(const auto &b : base_it->second)
                  queue.push_back(b);
              }
            }
            return false_exprt{};
          }
        }
      }
      return side_effect_expr_nondett{bool_typet{}, get_location(expr)};
    }
    return false_exprt{};
  }
  // abs()
  else if(func_name == "abs")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(!arg.is_nil())
      {
        // PLib builtins: abs(complex) = sqrt(real² + imag²)
        if(
          arg.type().id() == ID_struct &&
          to_struct_type(arg.type()).get_tag() == "python_complex")
        {
          // For struct_exprt (literal complex), compute at conversion time
          if(arg.id() == ID_struct && arg.operands().size() == 2)
          {
            const exprt &re = arg.operands()[0];
            const exprt &im = arg.operands()[1];
            if(re.is_constant() && im.is_constant())
            {
              ieee_floatt rv{
                ieee_float_spect::double_precision(),
                ieee_floatt::rounding_modet::ROUND_TO_EVEN};
              rv.from_expr(to_constant_expr(re));
              ieee_floatt iv{
                ieee_float_spect::double_precision(),
                ieee_floatt::rounding_modet::ROUND_TO_EVEN};
              iv.from_expr(to_constant_expr(im));
              double rd = std::stod(rv.to_ansi_c_string());
              double id = std::stod(iv.to_ansi_c_string());
              return double_to_floatbv(std::sqrt(rd * rd + id * id));
            }
          }
          // Variable complex: sqrt(real² + imag²)
          {
            member_exprt re{arg, "real", double_type()};
            member_exprt im{arg, "imag", double_type()};
            exprt sum = plus_exprt{mult_exprt{re, re}, mult_exprt{im, im}};
            // Return nondet >= 0 (sound overapproximation of sqrt)
            static unsigned abs_ctr = 0;
            std::string tn = "__abs_" + std::to_string(abs_ctr++);
            std::string tq = qualify_name(tn);
            irep_idt ti{tq};
            if(symbol_table.lookup(ti) == nullptr)
            {
              symbolt ts{ti, double_type(), "python"};
              ts.base_name = tn;
              ts.is_lvalue = true;
              ts.is_state_var = true;
              symbol_table.add(ts);
            }
            symbol_exprt tmp = symbol_table.lookup_ref(ti).symbol_expr();
            pending_checks.push_back(code_frontend_assignt{
              tmp,
              side_effect_expr_nondett{double_type(), source_locationt{}}});
            pending_checks.push_back(code_assumet{
              binary_relation_exprt{tmp, ID_ge, safe_zero(double_type())}});
            // Constrain: result² == real² + imag²
            pending_checks.push_back(
              code_assumet{ieee_float_equal_exprt{mult_exprt{tmp, tmp}, sum}});
            return std::move(tmp);
          }
        }
        // abs(x) = x >= 0 ? x : -x
        if(arg.type().id() == ID_signedbv || arg.type().id() == ID_floatbv)
        {
          return if_exprt{
            binary_relation_exprt{arg, ID_ge, safe_zero(arg.type())},
            arg,
            unary_minus_exprt{arg}};
        }
        // TypeError for non-numeric types
        if(
          is_python_string_type(arg.type()) ||
          is_python_list_type(arg.type()) || is_python_dict_type(arg.type()))
        {
          add_check(
            false_exprt{},
            "exception",
            "TypeError: bad operand type for abs()",
            get_location(expr));
        }
        return side_effect_expr_nondett{arg.type(), get_location(expr)};
      }
    }
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  // min(), max()
  else if(func_name == "min" || func_name == "max")
  {
    if(args.is_array() && as_array(args).size() >= 2)
    {
      auto it = as_array(args).begin();
      exprt a = convert_expression(*it);
      ++it;
      exprt b = convert_expression(*it);
      if(!a.is_nil() && !b.is_nil())
      {
        // Promote to same type (e.g., min(3, 2.5) → float)
        if(a.type() != b.type())
        {
          if(a.type().id() == ID_floatbv)
            b = safe_typecast(b, a.type());
          else if(b.type().id() == ID_floatbv)
            a = safe_typecast(a, b.type());
          else
            b = safe_typecast(b, a.type());
        }
        irep_idt op = (func_name == "min") ? ID_lt : ID_gt;
        return if_exprt{binary_relation_exprt{a, op, b}, a, b};
      }
    }
    return nil_exprt{};
  }

  // Regular function call — check if it's a class constructor
  if(class_types.count(func_name))
  {
    // Constructor call as expression: create temp, call __init__, return temp
    const struct_typet &cls_type = class_types[func_name];
    static unsigned ctor_tmp_counter = 0;
    std::string tmp_name =
      "__ctor_expr_" + func_name + "_" + std::to_string(ctor_tmp_counter++);
    std::string tmp_qname = qualify_name(tmp_name);
    irep_idt tmp_id{tmp_qname};

    if(symbol_table.lookup(tmp_id) == nullptr)
    {
      symbolt tmp_sym{tmp_id, cls_type, "python"};
      tmp_sym.base_name = tmp_name;
      tmp_sym.is_lvalue = true;
      tmp_sym.is_state_var = true;
      symbol_table.add(tmp_sym);
    }

    const symbolt &tmp_sym = symbol_table.lookup_ref(tmp_id);

    // Copy class-level default values from the class object
    irep_idt class_obj_id{"python::" + func_name};
    const symbolt *class_obj = symbol_table.lookup(class_obj_id);
    if(class_obj != nullptr && !class_obj->value.is_nil())
    {
      pending_checks.push_back(
        code_frontend_assignt{tmp_sym.symbol_expr(), class_obj->symbol_expr()});
    }

    irep_idt init_id{"python::" + func_name + "::__init__"};
    const symbolt *init_sym = symbol_table.lookup(init_id);
    if(init_sym != nullptr)
    {
      exprt::operandst init_args;
      init_args.push_back(address_of_exprt{tmp_sym.symbol_expr()});
      if(args.is_array())
      {
        for(const auto &arg : as_array(args))
          init_args.push_back(convert_expression(arg));
      }
      // Handle keyword arguments
      const code_typet &init_type = to_code_type(init_sym->type);
      const jsont &keywords = json_member(expr, "keywords");
      if(keywords.is_array())
      {
        for(const auto &kw : as_array(keywords))
        {
          std::string kw_name = json_string(json_member(kw, "arg"));
          exprt kw_val = convert_expression(json_member(kw, "value"));
          // Find the parameter index for this keyword
          for(std::size_t pi = 0; pi < init_type.parameters().size(); pi++)
          {
            if(id2string(init_type.parameters()[pi].get_base_name()) == kw_name)
            {
              while(init_args.size() <= pi)
                init_args.push_back(nil_exprt{});
              init_args[pi] = kw_val;
              break;
            }
          }
        }
      }
      // Pad missing args with defaults
      while(init_args.size() < init_type.parameters().size())
        init_args.push_back(
          safe_zero(init_type.parameters()[init_args.size()].type()));
      for(std::size_t i = 0;
          i < init_args.size() && i < init_type.parameters().size();
          i++)
      {
        if(init_args[i].type() != init_type.parameters()[i].type())
          init_args[i] =
            safe_typecast(init_args[i], init_type.parameters()[i].type());
      }

      side_effect_expr_function_callt call{
        init_sym->symbol_expr(),
        std::move(init_args),
        empty_typet{},
        get_location(expr)};
      // Inject the __init__ call before the current statement
      pending_checks.push_back(code_expressiont{call});
    }

    return tmp_sym.symbol_expr();
  }

  irep_idt symbol_id{"python::" + func_name};
  const symbolt *sym = symbol_table.lookup(symbol_id);
  // The former ad-hoc math-function block has been retired.
  // math.py declares each function with @c_intrinsic('name',
  // fold='op', domain='kind', range='kind').
  // The decorator path a few hundred lines below handles the
  // complete semantics: parse-time fold for constants, guarded
  // ValueError for domain violations, and range-constrained
  // nondet return for symbolic arguments.

  // Check function aliases (lambda assignments: double = lambda x: x*2)
  if(sym == nullptr || sym->type.id() != ID_code)
  {
    auto alias_it = function_aliases.find(qualify_name(func_name));
    if(alias_it != function_aliases.end())
      sym = symbol_table.lookup(alias_it->second);
  }

  if(sym == nullptr || sym->type.id() != ID_code)
  {
    // Try module scope (forward references from inside functions)
    if(!current_function.empty())
    {
      sym = symbol_table.lookup(irep_idt{"python::" + func_name});
      if(sym != nullptr && sym->type.id() != ID_code)
        sym = nullptr;
    }
  }

  // PLR §3.3.5: callable instances via __call__. If the
  // 'function' is actually a symbol naming a class
  // instance (struct-typed), look up its __call__
  // method and emit a method call.
  if(sym == nullptr || sym->type.id() != ID_code)
  {
    const symbolt *var_sym =
      symbol_table.lookup(irep_idt{qualify_name(func_name)});
    if(var_sym == nullptr)
      var_sym = symbol_table.lookup(irep_idt{"python::" + func_name});
    if(var_sym != nullptr)
    {
      typet inst_type = var_sym->type;
      std::string tag;
      if(inst_type.id() == ID_struct)
        tag = id2string(to_struct_type(inst_type).get_tag());
      else if(inst_type.id() == ID_struct_tag)
        tag = id2string(to_struct_tag_type(inst_type).get_identifier());
      if(tag.substr(0, 13) == "python_class_")
      {
        std::string bare = tag.substr(13);
        for(const std::string &prefix :
            {std::string{"python::"} + tag + "::__call__",
             std::string{"python::"} + bare + "::__call__"})
        {
          const symbolt *cs = symbol_table.lookup(irep_idt{prefix});
          if(cs != nullptr)
          {
            typet ret_type = python_int_type();
            if(cs->type.id() == ID_code)
              ret_type = to_code_type(cs->type).return_type();
            exprt::operandst call_args;
            call_args.push_back(address_of_exprt{var_sym->symbol_expr()});
            if(args.is_array())
            {
              for(const auto &arg : as_array(args))
                call_args.push_back(convert_expression(arg));
            }
            return side_effect_expr_function_callt{
              cs->symbol_expr(),
              std::move(call_args),
              ret_type,
              get_location(expr)};
          }
        }
      }
    }
  }

  if(sym == nullptr || sym->type.id() != ID_code)
  {
    // Recognised-but-unmodelled Python built-ins. Returning a sound
    // nondet value is safe for these because (i) they are pure or
    // have no side effects that affect verification targets, and
    // (ii) any further reasoning about them would require a proper
    // model (tracked by Step 2 of the module support plan). By
    // whitelisting them here we avoid the spurious "no-body" false
    // positive that would otherwise fail verification of any
    // program that merely mentions them.
    //
    // Each entry maps the Python name to the type of the nondet
    // value returned. A nil typet means: use python_int_type().
    static const std::map<std::string, typet> known_nondet_builtins = {
      {"getattr", typet{}},
      {"setattr", typet{}},
      {"hasattr", bool_typet{}},
      {"callable", bool_typet{}},
      {"issubclass", bool_typet{}},
      {"id", typet{}},
      {"hash", typet{}},
      {"iter", typet{}},
      {"next", typet{}},
      {"tuple", typet{}},
      {"list", typet{}},
      {"set", typet{}},
      {"frozenset", typet{}},
      {"dict", typet{}},
      {"bytes", typet{}},
      {"bytearray", typet{}},
      {"memoryview", typet{}},
      {"super", typet{}},
      {"object", typet{}},
      {"classmethod", typet{}},
      {"staticmethod", typet{}},
      {"property", typet{}},
      {"vars", typet{}},
      {"dir", typet{}},
      {"globals", typet{}},
      {"locals", typet{}},
      {"open", typet{}},
      {"type", typet{}},
    };
    auto builtin_it = known_nondet_builtins.find(func_name);
    if(builtin_it != known_nondet_builtins.end())
    {
      // A default-constructed typet in the table means "use the
      // generic python_int_type()"; otherwise use the explicit type.
      // typet{}.is_nil() is false (the default id is empty, not
      // ID_nil), so check for an empty id instead.
      const typet t = builtin_it->second.id().empty() ? python_int_type()
                                                      : builtin_it->second;
      side_effect_expr_nondett nondet{t, get_location(expr)};
      return std::move(nondet);
    }

    // Unknown function — return nondet value (sound overapproximation).
    // Quiet by default because stdlib ingestion routinely hits
    // hundreds of these and the fallback is already correct (nondet
    // + optional no-body assertion). Visible with --verbosity 9 or
    // --python-strict-warnings.
    log_overapprox(
      "function '" + func_name +
      "': no body known, returning nondet over-approximation");
    // Suppress the no-body property for names that came from a
    // failed-to-resolve import. The user can't provide a body
    // for them and the tool can't be sound about their
    // behaviour; treat the call as nondet (sound
    // over-approximation) without flagging a property.
    if(!no_body_check && unresolved_imports.count(func_name) == 0)
    {
      add_check(
        false_exprt{},
        "no-body",
        "no body for callee " + func_name,
        get_location(expr));
    }
    side_effect_expr_nondett nondet{python_int_type(), get_location(expr)};
    return std::move(nondet);
  }

  const code_typet &func_type = to_code_type(sym->type);
  const auto &params = func_type.parameters();

  // Build argument list: start with positional args
  exprt::operandst arguments;
  if(args.is_array())
  {
    for(const auto &arg : as_array(args))
    {
      // PEP 448: f(*c) — spread known list literals at
      // conversion time. Non-literal iterables are
      // over-approximated by appending a single nondet.
      if(is_node_type(arg, "Starred"))
      {
        exprt inner = convert_expression(json_member(arg, "value"));
        if(inner.is_nil())
        {
          arguments.push_back(nil_exprt{});
          continue;
        }
        const exprt *lit = nullptr;
        if(inner.id() == ID_struct && inner.operands().size() >= 2)
          lit = &inner;
        else if(inner.id() == ID_symbol)
        {
          auto it = list_literals.find(to_symbol_expr(inner).get_identifier());
          if(it != list_literals.end())
            lit = &it->second;
        }
        if(
          lit != nullptr && lit->operands().size() >= 2 &&
          lit->operands()[0].is_constant())
        {
          mp_integer len_val;
          if(!to_integer(to_constant_expr(lit->operands()[0]), len_val))
          {
            const exprt &data_arr = lit->operands()[1];
            std::size_t n = len_val.to_ulong();
            for(std::size_t i = 0; i < n && i < data_arr.operands().size(); i++)
              arguments.push_back(data_arr.operands()[i]);
            continue;
          }
        }
        log_overapprox(
          "PEP 448 call unpacking of non-literal iterable — nondet");
        arguments.push_back(
          side_effect_expr_nondett{python_int_type(), source_locationt{}});
        continue;
      }
      arguments.push_back(convert_expression(arg));
    }
  }

  // Handle keyword arguments: match by parameter name
  const jsont &keywords = json_member(expr, "keywords");
  if(keywords.is_array() && !as_array(keywords).empty())
  {
    arguments.resize(params.size(), nil_exprt{});

    // Collect unmatched keywords for **kwargs
    std::vector<std::pair<std::string, exprt>> unmatched_kw;

    for(const auto &kw : as_array(keywords))
    {
      std::string kw_name = json_string(json_member(kw, "arg"));
      exprt kw_val = convert_expression(json_member(kw, "value"));

      // PEP 448: f(**d) — kw_name is empty. Spread the dict's
      // known keys into individual entries. If the dict's
      // content isn't known at conversion time, register a
      // single nondet entry under the name '*' to signal
      // 'kwargs may have arbitrary extra keys'.
      if(kw_name.empty())
      {
        const exprt *lit = nullptr;
        if(kw_val.id() == ID_struct && kw_val.operands().size() >= 3)
          lit = &kw_val;
        else if(kw_val.id() == ID_symbol)
        {
          auto it = dict_literals.find(to_symbol_expr(kw_val).get_identifier());
          if(it != dict_literals.end())
            lit = &it->second;
        }
        if(
          lit != nullptr && lit->operands().size() >= 3 &&
          lit->operands()[0].is_constant())
        {
          mp_integer len_val;
          if(!to_integer(to_constant_expr(lit->operands()[0]), len_val))
          {
            const exprt &keys_arr = lit->operands()[1];
            const exprt &vals_arr = lit->operands()[2];
            std::size_t n = len_val.to_ulong();
            for(std::size_t i = 0; i < n && i < keys_arr.operands().size(); i++)
            {
              auto kv = extract_string_value(keys_arr.operands()[i]);
              if(!kv.has_value())
                continue;
              // Try matching against params, else add to
              // unmatched_kw for packing.
              bool matched = false;
              for(std::size_t j = 0; j < params.size(); j++)
              {
                if(id2string(params[j].get_base_name()) == kv.value())
                {
                  // Typecast spread value to declared param type
                  // (see comment in the method-call branch above).
                  exprt v = vals_arr.operands()[i];
                  if(v.type() != params[j].type())
                    v = safe_typecast(v, params[j].type());
                  arguments[j] = std::move(v);
                  matched = true;
                  break;
                }
              }
              if(!matched)
                unmatched_kw.push_back({kv.value(), vals_arr.operands()[i]});
            }
            continue;
          }
        }
        // Unknown dict contents: skip (over-approx — kwargs
        // will miss these, required-kwarg checks may FP).
        log_overapprox(
          "**dict spread of non-literal dict — kwargs left under-populated");
        continue;
      }

      bool matched = false;
      for(std::size_t i = 0; i < params.size(); i++)
      {
        if(id2string(params[i].get_base_name()) == kw_name)
        {
          exprt v = kw_val;
          if(v.type() != params[i].type())
            v = safe_typecast(v, params[i].type());
          arguments[i] = std::move(v);
          matched = true;
          break;
        }
      }
      if(!matched)
        unmatched_kw.push_back({kw_name, kw_val});
    }

    // Pack unmatched keywords into a dict for the last param if it's a dict
    if(
      !unmatched_kw.empty() && !params.empty() &&
      is_python_dict_type(params.back().type()))
    {
      std::size_t kwargs_idx = params.size() - 1;
      typet dict_type = params.back().type();
      const auto &dict_st = to_struct_type(dict_type);
      const auto &keys_arr_type = to_array_type(dict_st.components()[1].type());
      const auto &vals_arr_type = to_array_type(dict_st.components()[2].type());

      exprt::operandst key_elems, val_elems;
      for(const auto &[name, val] : unmatched_kw)
      {
        key_elems.push_back(python_string_literal(name));
        if(is_python_value_type(vals_arr_type.element_type()))
          val_elems.push_back(wrap_value(val));
        else
          val_elems.push_back(safe_typecast(val, vals_arr_type.element_type()));
      }
      while(key_elems.size() < PYTHON_MAX_DICT_SIZE)
      {
        key_elems.push_back(safe_zero(keys_arr_type.element_type()));
        val_elems.push_back(safe_zero(vals_arr_type.element_type()));
      }
      exprt length = from_integer(
        static_cast<long long>(unmatched_kw.size()), signedbv_typet{64});
      arguments[kwargs_idx] = struct_exprt{
        {length,
         array_exprt{std::move(key_elems), keys_arr_type},
         array_exprt{std::move(val_elems), vals_arr_type}},
        dict_type};
    }
  }

  // Prepend bound self for bound method calls
  {
    auto bm_it = bound_methods.find(qualify_name(func_name));
    if(bm_it != bound_methods.end())
    {
      arguments.insert(arguments.begin(), bm_it->second.second);
    }
  }

  // Add closure captures as extra arguments BEFORE padding
  {
    auto cap_it2 = closure_captures.find(id2string(sym->name));
    if(cap_it2 != closure_captures.end())
    {
      for(const auto &[outer_id, name, type] : cap_it2->second)
      {
        const symbolt *outer_sym = symbol_table.lookup(irep_idt{outer_id});
        if(outer_sym != nullptr)
          arguments.push_back(outer_sym->symbol_expr());
        else
          arguments.push_back(
            side_effect_expr_nondett{type, get_location(expr)});
      }
    }
  }

  // Fill in defaults for any remaining nil arguments.
  // Defaults are stored in the FunctionDef AST; look up the function's
  // definition to find them.
  if(arguments.size() < params.size())
    arguments.resize(params.size(), nil_exprt{});

  // Use pre-evaluated default values (evaluated at definition time)
  for(std::size_t i = 0; i < arguments.size() && i < params.size(); i++)
  {
    if(arguments[i].is_nil())
    {
      auto def_it = default_values.find({func_name, i});
      if(def_it != default_values.end())
      {
        arguments[i] = def_it->second;
        // Class reference: struct default → pointer param
        if(
          params[i].type().id() == ID_pointer &&
          arguments[i].type().id() == ID_struct &&
          to_pointer_type(params[i].type()).base_type() == arguments[i].type())
        {
          arguments[i] = address_of_exprt{arguments[i]};
        }
      }
    }
  }

  // Fallback: look up the function's AST to get defaults
  const jsont &body = json_member(parse_tree.ast_json, "body");
  if(body.is_array())
  {
    for(const auto &stmt : as_array(body))
    {
      if(
        (is_node_type(stmt, "FunctionDef") ||
         is_node_type(stmt, "AsyncFunctionDef")) &&
        json_string(json_member(stmt, "name")) == func_name)
      {
        const jsont &func_args = json_member(stmt, "args");
        const jsont &defaults = json_member(func_args, "defaults");
        if(defaults.is_array())
        {
          std::size_t n_defaults = as_array(defaults).size();
          std::size_t first_default = params.size() - n_defaults;
          auto def_it = as_array(defaults).begin();
          for(std::size_t i = first_default; i < params.size(); i++, ++def_it)
          {
            if(arguments[i].is_nil())
            {
              arguments[i] = convert_expression(*def_it);
              // Class reference: struct default → pointer param
              if(
                params[i].type().id() == ID_pointer &&
                arguments[i].type().id() == ID_struct &&
                to_pointer_type(params[i].type()).base_type() ==
                  arguments[i].type())
              {
                arguments[i] = address_of_exprt{arguments[i]};
              }
            }
          }
        }
        break;
      }
    }
  }

  // Replace any remaining nil arguments with safe_zero of param type
  for(std::size_t i = 0; i < arguments.size() && i < params.size(); i++)
  {
    if(arguments[i].is_nil())
    {
      if(params[i].type().id() == ID_pointer)
      {
        // For pointer params (class references), create a temp object
        static unsigned ref_tmp_ctr = 0;
        std::string tn = "__ref_tmp_" + std::to_string(ref_tmp_ctr++);
        std::string tq = qualify_name(tn);
        irep_idt ti{tq};
        typet base = to_pointer_type(params[i].type()).base_type();
        if(symbol_table.lookup(ti) == nullptr)
        {
          symbolt ts{ti, base, "python"};
          ts.base_name = tn;
          ts.is_lvalue = true;
          ts.is_state_var = true;
          symbol_table.add(ts);
        }
        arguments[i] =
          address_of_exprt{symbol_table.lookup_ref(ti).symbol_expr()};
      }
      else
        arguments[i] = safe_zero(params[i].type());
    }
  }

  // Remove trailing nil arguments beyond param count
  while(!arguments.empty() && arguments.back().is_nil())
    arguments.pop_back();

  // Typecast arguments to match parameter types
  for(std::size_t i = 0; i < arguments.size() && i < params.size(); i++)
  {
    if(!arguments[i].is_nil() && arguments[i].type() != params[i].type())
    {
      // Class reference: struct arg → pointer param → take address_of
      if(
        params[i].type().id() == ID_pointer &&
        arguments[i].type().id() == ID_struct &&
        to_pointer_type(params[i].type()).base_type() == arguments[i].type())
      {
        arguments[i] = address_of_exprt{arguments[i]};
      }
      else
      {
        // PLR soundness: emit annotation-mismatch property at
        // the call site when the argument's type is obviously
        // incompatible with the parameter's declared type.
        //
        // Skip when the parameter is list-typed and argument is
        // not — this is almost certainly a vararg (*args)
        // collection, not an annotation mismatch.
        bool is_likely_vararg_collect =
          is_python_list_type(params[i].type()) &&
          !is_python_list_type(arguments[i].type());
        if(
          python_check_annotations && !is_likely_vararg_collect &&
          annotation_types_incompatible(params[i].type(), arguments[i].type()))
        {
          add_check(
            false_exprt{},
            "annotation-mismatch",
            "argument " + std::to_string(i) +
              "'s type does not match declared parameter type of '" +
              func_name + "'",
            get_location(expr));
        }
        arguments[i] = safe_typecast(arguments[i], params[i].type());
      }
    }
  }

  // @c_intrinsic: redirect the call to the named C function. The
  // Python function's declared signature is used as-is for the C
  // intrinsic, with one exception — Python str parameters (and
  // str returns) are marshalled to/from C ``char *`` so the C
  // library's view of string arguments is consistent with its
  // usual conventions. See the 'str marshalling' comments below.
  auto intrinsic_it = c_intrinsic_map.find(sym->name);
  if(intrinsic_it != c_intrinsic_map.end())
  {
    const std::string &c_name = intrinsic_it->second;

    // Parse-time constant folding. If a ``fold=`` keyword was
    // set on the decorator *and* every argument at this call
    // site is a float (or int-that-converts-to-float) constant,
    // evaluate the named host-side op (from <cmath>) and return
    // the result as a constant expression. Falls through to the
    // C-call path for non-constant arguments or unrecognised
    // fold names.
    //
    // If ``domain=`` is also set, a constant argument that fails
    // the named domain predicate raises Python ValueError
    // (matching CPython's math-domain semantics). A nondet
    // argument leaves the ad-hoc math path (in
    // imported_math_funcs) to emit the guarded ValueError and
    // the nondet+constraints return. This duplication will be
    // retired in a follow-up once the decorator supports the
    // nondet+constraints case too.
    auto fold_it = c_intrinsic_fold_map.find(sym->name);
    auto domain_it = c_intrinsic_domain_map.find(sym->name);
    if(fold_it != c_intrinsic_fold_map.end() && arguments.size() == 1)
    {
      auto cv = try_eval_double(arguments[0]);
      if(cv.has_value())
      {
        double x = cv.value();

        // Domain check for constant args. If out of domain, raise
        // ValueError (matching CPython) and return a nondet
        // sentinel; the exception handler intercepts before the
        // caller observes the value.
        if(domain_it != c_intrinsic_domain_map.end())
        {
          const std::string &dom = domain_it->second;
          bool in_domain = true;
          if(dom == "nonneg")
            in_domain = x >= 0;
          else if(dom == "positive")
            in_domain = x > 0;
          else if(dom == "gt_neg_one")
            in_domain = x > -1;
          else if(dom == "abs_le_1")
            in_domain = x >= -1 && x <= 1;
          else if(dom == "abs_lt_1")
            in_domain = x > -1 && x < 1;
          else if(dom == "ge_1")
            in_domain = x >= 1;
          if(!in_domain)
          {
            emit_value_error(false_exprt{});
            return side_effect_expr_nondett{double_type(), get_location(expr)};
          }
        }

        const std::string &op = fold_it->second;
        double r = 0;
        bool computed = true;
        if(op == "sqrt")
          r = std::sqrt(x);
        else if(op == "cbrt")
          r = std::cbrt(x);
        else if(op == "exp")
          r = std::exp(x);
        else if(op == "exp2")
          r = std::exp2(x);
        else if(op == "expm1")
          r = std::expm1(x);
        else if(op == "log")
          r = std::log(x);
        else if(op == "log2")
          r = std::log2(x);
        else if(op == "log10")
          r = std::log10(x);
        else if(op == "log1p")
          r = std::log1p(x);
        else if(op == "sin")
          r = std::sin(x);
        else if(op == "cos")
          r = std::cos(x);
        else if(op == "tan")
          r = std::tan(x);
        else if(op == "asin")
          r = std::asin(x);
        else if(op == "acos")
          r = std::acos(x);
        else if(op == "atan")
          r = std::atan(x);
        else if(op == "sinh")
          r = std::sinh(x);
        else if(op == "cosh")
          r = std::cosh(x);
        else if(op == "tanh")
          r = std::tanh(x);
        else if(op == "asinh")
          r = std::asinh(x);
        else if(op == "acosh")
          r = std::acosh(x);
        else if(op == "atanh")
          r = std::atanh(x);
        else if(op == "ceil")
          r = std::ceil(x);
        else if(op == "floor")
          r = std::floor(x);
        else if(op == "trunc")
          r = std::trunc(x);
        else if(op == "fabs")
          r = std::fabs(x);
        else if(op == "erf")
          r = std::erf(x);
        else if(op == "erfc")
          r = std::erfc(x);
        else if(op == "gamma" || op == "tgamma")
          r = std::tgamma(x);
        else if(op == "lgamma")
          r = std::lgamma(x);
        else
          computed = false;
        if(computed && std::isfinite(r))
          return double_to_floatbv(r);
      }
    }

    // Two-arg fold: math.pow(x, y), atan2(y, x), hypot(x, y),
    // fmod(x, y), copysign(x, y), remainder(x, y). All args
    // must be float/int constants; when both are, evaluate
    // via std::<op>.
    if(fold_it != c_intrinsic_fold_map.end() && arguments.size() == 2)
    {
      auto cv1 = try_eval_double(arguments[0]);
      auto cv2 = try_eval_double(arguments[1]);
      if(cv1.has_value() && cv2.has_value())
      {
        const std::string &op = fold_it->second;
        double a = cv1.value(), b = cv2.value();
        double r = 0;
        bool computed = true;
        if(op == "pow")
          r = std::pow(a, b);
        else if(op == "atan2")
          r = std::atan2(a, b);
        else if(op == "hypot")
          r = std::hypot(a, b);
        else if(op == "fmod")
          r = std::fmod(a, b);
        else if(op == "copysign")
          r = std::copysign(a, b);
        else if(op == "remainder")
          r = std::remainder(a, b);
        else
          computed = false;
        if(computed && std::isfinite(r))
          return double_to_floatbv(r);
      }
    }

    // Symbolic-argument handling for decorator-driven math.
    //
    // When fold= is set but the argument is symbolic (not a
    // compile-time constant), we model the call as a nondet
    // return constrained by the optional domain= and range=
    // keywords — matching the semantics of CPython's math
    // functions plus CBMC's C math-library model:
    //
    //   * domain= named predicate: emit guarded ValueError if
    //     the predicate rejects the argument at runtime.
    //   * range= named predicate: constrain the nondet return
    //     accordingly.
    //
    // Return type follows func_type (the Python declaration).
    // For most math functions this is double_type().
    auto range_it = c_intrinsic_range_map.find(sym->name);
    bool is_math_float_fn =
      fold_it != c_intrinsic_fold_map.end() && arguments.size() == 1 &&
      to_code_type(func_type).return_type().id() == ID_floatbv;
    if(is_math_float_fn)
    {
      std::string dom = domain_it != c_intrinsic_domain_map.end()
                          ? domain_it->second
                          : std::string{};
      std::string rng = range_it != c_intrinsic_range_map.end()
                          ? range_it->second
                          : std::string{};
      return emit_math_intrinsic_nondet(
        dom, rng, arguments[0], get_location(expr));
    }

    const pointer_typet c_char_ptr{char_type(), config.ansi_c.pointer_width};

    // Helper: is this type a Python refined string type?
    auto is_py_str = [](const typet &t) { return is_python_string_type(t); };

    // Build the C function signature by projecting each Python
    // parameter onto a C equivalent. Python str → C char*.
    // Python int → C signed int of the width declared by the
    // @c_intrinsic('name', int_width=N) annotation (default 64).
    auto iw_it = c_intrinsic_int_width_map.find(sym->name);
    int int_width =
      iw_it != c_intrinsic_int_width_map.end() ? iw_it->second : 64;
    auto maybe_narrow_int = [&](typet &t)
    {
      if(t.id() == ID_signedbv)
      {
        const auto sz = to_signedbv_type(t).get_width();
        if(
          static_cast<int>(sz) != int_width &&
          (int_width == 32 || int_width == 64))
          t = signedbv_typet{static_cast<std::size_t>(int_width)};
      }
    };
    code_typet c_func_type = to_code_type(func_type);
    for(auto &p : c_func_type.parameters())
    {
      if(is_py_str(p.type()))
        p.type() = c_char_ptr;
      else
        maybe_narrow_int(p.type());
      p.set_identifier(irep_idt{});
    }
    typet c_return_type = c_func_type.return_type();
    const bool return_is_py_str = is_py_str(c_return_type);
    if(return_is_py_str)
      c_func_type.return_type() = c_char_ptr;
    else
      maybe_narrow_int(c_func_type.return_type());

    irep_idt c_id{c_name};
    if(symbol_table.lookup(c_id) == nullptr)
    {
      symbolt c_sym{c_id, c_func_type, ID_C};
      c_sym.base_name = c_name;
      c_sym.location = get_location(expr);
      c_sym.is_lvalue = true;
      c_sym.is_extern = true;
      symbol_table.add(c_sym);
    }

    // Marshal arguments: for each parameter typed as Python str,
    // extract the struct's 'data' pointer and hand that to C.
    // For string-literal arguments (produced by
    // build_string_struct), the backing array is a *temporary*
    // and taking its address produces a pointer CBMC treats as a
    // "dead object" when dereferenced later. build_string_literal
    // wraps the same bytes into a static-lifetime symbol and
    // returns a new struct with the safe pointer; we detect the
    // inline literal shape and substitute, then extract the raw
    // pointer without going through a struct temporary (that
    // temporary loses track of the persistent storage).
    const auto &c_params = c_func_type.parameters();
    for(std::size_t i = 0; i < arguments.size() && i < c_params.size(); i++)
    {
      const typet &py_param_type =
        to_code_type(func_type).parameters()[i].type();
      if(is_py_str(py_param_type))
      {
        // Try to recognise the build_string_struct shape:
        //   { length-constant, address_of(array-literal[0]) }
        // If matched, hand C the address_of directly — that's a
        // genuine pointer into persistent storage, not a
        // member-access into a temporary struct.
        if(
          arguments[i].id() == ID_struct && arguments[i].operands().size() == 2)
        {
          const exprt &len_op = arguments[i].operands()[0];
          const exprt &data_op = arguments[i].operands()[1];
          if(
            len_op.is_constant() && data_op.id() == ID_address_of &&
            to_address_of_expr(data_op).object().id() == ID_index)
          {
            const exprt &arr =
              to_index_expr(to_address_of_expr(data_op).object()).array();
            if(arr.id() == ID_array)
            {
              std::string bytes;
              bytes.reserve(arr.operands().size());
              bool all_bytes = true;
              for(const auto &op : arr.operands())
              {
                if(!op.is_constant())
                {
                  all_bytes = false;
                  break;
                }
                mp_integer v;
                if(to_integer(to_constant_expr(op), v))
                {
                  all_bytes = false;
                  break;
                }
                bytes.push_back(static_cast<char>(v.to_long()));
              }
              // (build_string_struct doesn't append a trailing
              // NUL — string_constantt below handles that.)
              if(all_bytes)
              {
                // Emit a C string_constantt — CBMC recognises
                // these as persistent and exempt from dead-object
                // checks, exactly like C code's "literal"
                // constructs. Pass address_of(str[0]) as the
                // C callee's char*.
                string_constantt sc{irep_idt{bytes}};
                arguments[i] = typecast_exprt{
                  address_of_exprt{index_exprt{
                    sc, from_integer(0, signedbv_typet{64}), char_type()}},
                  c_char_ptr};
                continue;
              }
            }
          }
        }

        exprt data = member_exprt{
          arguments[i],
          "data",
          pointer_typet{unsignedbv_typet{8}, config.ansi_c.pointer_width}};
        arguments[i] = typecast_exprt{std::move(data), c_char_ptr};
      }
      else if(
        arguments[i].type().id() == ID_signedbv &&
        c_params[i].type().id() == ID_signedbv &&
        arguments[i].type() != c_params[i].type())
      {
        // int width narrowing / widening for int_width= callees.
        arguments[i] = typecast_exprt{arguments[i], c_params[i].type()};
      }
    }

    symbol_exprt callee_expr = symbol_table.lookup_ref(c_id).symbol_expr();
    callee_expr.add_source_location() = get_location(expr);
    exprt call = side_effect_expr_function_callt{
      std::move(callee_expr),
      std::move(arguments),
      c_func_type.return_type(),
      get_location(expr)};

    if(!return_is_py_str)
    {
      // If int_width= narrowed the return type, widen back to
      // the Python-declared int so downstream code gets the
      // expected signedbv width.
      const typet &py_return = to_code_type(func_type).return_type();
      if(
        call.type().id() == ID_signedbv && py_return.id() == ID_signedbv &&
        call.type() != py_return)
      {
        return typecast_exprt{std::move(call), py_return};
      }
      return call;
    }

    // Marshal the return: wrap the returned char* into a Python
    // refined-string struct. The length is nondet (we can't
    // compute strlen precisely without a separate intrinsic), but
    // for a sound verification over-approximation we constrain it
    // to be in [0, PYTHON_MAX_STRING_LENGTH].
    // We store the call's result in a fresh temp first because
    // we want to read it multiple times (for the data and the
    // fake length) without duplicating side effects.
    static unsigned cstr_ret_ctr = 0;
    std::string rn = "__cstr_ret_" + std::to_string(cstr_ret_ctr++);
    std::string rq = qualify_name(rn);
    irep_idt rid{rq};
    if(symbol_table.lookup(rid) == nullptr)
    {
      symbolt rs{rid, c_char_ptr, "python"};
      rs.base_name = rn;
      rs.is_lvalue = true;
      rs.is_state_var = true;
      symbol_table.add(rs);
    }
    symbol_exprt ret_ptr = symbol_table.lookup_ref(rid).symbol_expr();
    pending_checks.push_back(code_frontend_assignt{ret_ptr, call});

    // Nondet length, constrained to [0, PYTHON_MAX_STRING_LENGTH].
    std::string ln = "__cstr_len_" + std::to_string(cstr_ret_ctr - 1);
    std::string lq = qualify_name(ln);
    irep_idt lid{lq};
    if(symbol_table.lookup(lid) == nullptr)
    {
      symbolt ls{lid, signedbv_typet{64}, "python"};
      ls.base_name = ln;
      ls.is_lvalue = true;
      ls.is_state_var = true;
      symbol_table.add(ls);
    }
    symbol_exprt ret_len = symbol_table.lookup_ref(lid).symbol_expr();
    pending_checks.push_back(code_frontend_assignt{
      ret_len,
      side_effect_expr_nondett{signedbv_typet{64}, source_locationt{}}});
    pending_checks.push_back(code_assumet{binary_relation_exprt{
      ret_len, ID_ge, from_integer(0, signedbv_typet{64})}});
    pending_checks.push_back(code_assumet{binary_relation_exprt{
      ret_len,
      ID_le,
      from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64})}});

    // Build the Python refined-string struct {length, data} where
    // data is the C pointer we just saved.
    return struct_exprt{
      {ret_len,
       typecast_exprt{
         ret_ptr,
         pointer_typet{unsignedbv_typet{8}, config.ansi_c.pointer_width}}},
      python_string_type()};
  }

  side_effect_expr_function_callt call{
    sym->symbol_expr(),
    std::move(arguments),
    func_type.return_type(),
    get_location(expr)};

  return std::move(call);
}
