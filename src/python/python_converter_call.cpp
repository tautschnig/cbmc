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

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
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
          // PLR §3.3.2.1: for value-returning super().method(),
          // emit a direct call to <base>::<method> with the
          // current self pointer. Inlining the body (used
          // unconditionally before) early-returns from the
          // CALLER's function via the inlined `return X` —
          // wrong for `super().get_value() + 1`, where the
          // caller wants the value, not the early return.
          //
          // The original inlining strategy is retained for
          // __init__ where it's needed: __init__ has no useful
          // return value but DOES rely on running in the
          // caller's scope (so self refers to the Derived
          // instance, not a fresh Base).
          if(method_name != "__init__")
          {
            irep_idt base_method_id{
              "python::" + base_class + "::" + method_name};
            const symbolt *base_method = symbol_table.lookup(base_method_id);
            if(
              base_method != nullptr && base_method->type.id() == ID_code &&
              !current_function.empty())
            {
              const auto &mt = to_code_type(base_method->type);
              const auto &mparams = mt.parameters();
              exprt::operandst arguments;
              if(!mparams.empty())
              {
                // Pass the caller's self as the base method's
                // self. The caller's self is a parameter of the
                // current method; look it up by id.
                irep_idt caller_self{"python::" + current_function + "::self"};
                const symbolt *self_sym = symbol_table.lookup(caller_self);
                if(self_sym != nullptr)
                {
                  exprt self_arg = self_sym->symbol_expr();
                  if(self_arg.type() != mparams[0].type())
                    self_arg = safe_typecast(self_arg, mparams[0].type());
                  arguments.push_back(std::move(self_arg));
                }
                else
                {
                  arguments.push_back(side_effect_expr_nondett{
                    mparams[0].type(), get_location(expr)});
                }
              }
              const jsont &super_args = json_member(expr, "args");
              if(super_args.is_array())
              {
                for(const auto &a : as_array(super_args))
                {
                  exprt av = convert_expression(a);
                  arguments.push_back(std::move(av));
                }
              }
              for(std::size_t ai = 0;
                  ai < arguments.size() && ai < mparams.size();
                  ++ai)
              {
                if(arguments[ai].type() != mparams[ai].type())
                  arguments[ai] =
                    safe_typecast(arguments[ai], mparams[ai].type());
              }
              return side_effect_expr_function_callt{
                base_method->symbol_expr(),
                std::move(arguments),
                mt.return_type(),
                get_location(expr)};
            }
          }

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
      // PLR §4.4.2: int.from_bytes(b, byteorder, *, signed=False)
      //             int.to_bytes(self, length, byteorder, *, signed=False)
      // Class-method dispatch on the int built-in. The byteorder
      // is treated as a string literal at the call site
      // ("big" / "little"); a runtime byteorder selector would
      // need a 2× expression and is not exercised by any
      // current test. signed=True is uncommon; we treat it as
      // signed=False (sound for non-negative values, the
      // common case). Placed BEFORE imported_modules check
      // because 'int' is a built-in, not an imported module.
      if(obj_name == "int")
      {
        // int.to_bytes(x, length, byteorder)
        // Returns a python_list_type(uint8) of `length` bytes.
        if(method_name == "to_bytes" && args.is_array())
        {
          auto a_it = as_array(args).begin();
          auto a_end = as_array(args).end();
          if(a_it != a_end)
          {
            exprt x = convert_expression(*a_it++);
            if(x.type() != python_int_type())
              x = safe_typecast(x, python_int_type());

            std::size_t n_len = 0;
            if(a_it != a_end)
            {
              exprt n_expr = convert_expression(*a_it++);
              if(n_expr.is_constant())
              {
                mp_integer iv;
                if(!to_integer(to_constant_expr(n_expr), iv))
                  n_len = iv.to_ulong();
              }
            }
            std::string order = "big";
            if(a_it != a_end)
            {
              if(is_node_type(*a_it, "Constant"))
              {
                const jsont &v = json_member(*a_it, "value");
                if(v.is_string())
                  order = json_string(v);
              }
            }

            if(n_len == 0 || n_len > 16)
            {
              return side_effect_expr_nondett{
                python_list_type(unsignedbv_typet{8}), get_location(expr)};
            }

            const typet u8 = unsignedbv_typet{8};
            const typet i64 = python_int_type();
            const std::size_t buf_len =
              std::max<std::size_t>(PYTHON_MAX_LIST_LENGTH, n_len);
            array_typet data_t{u8, from_integer(buf_len, i64)};

            // For big-endian: data[i] = (x >> ((n-1-i)*8)) & 0xff
            // For little-endian: data[i] = (x >> (i*8)) & 0xff
            exprt::operandst bytes_ops;
            for(std::size_t i = 0; i < n_len; i++)
            {
              std::size_t shift = (order == "little") ? i : (n_len - 1 - i);
              exprt shifted =
                lshr_exprt{x, from_integer(static_cast<long>(shift) * 8, i64)};
              exprt masked = bitand_exprt{shifted, from_integer(0xff, i64)};
              bytes_ops.push_back(typecast_exprt{masked, u8});
            }
            while(bytes_ops.size() < buf_len)
              bytes_ops.push_back(from_integer(0, u8));

            array_exprt data_arr{std::move(bytes_ops), data_t};
            return struct_exprt{
              {from_integer(n_len, i64), data_arr}, python_list_type(u8)};
          }
        }
        // int.from_bytes(b, byteorder, signed=False)
        // Returns an int read from the bytes object b.
        if(method_name == "from_bytes" && args.is_array())
        {
          auto a_it = as_array(args).begin();
          auto a_end = as_array(args).end();
          if(a_it == a_end)
            return side_effect_expr_nondett{
              python_int_type(), get_location(expr)};

          exprt b = convert_expression(*a_it++);
          std::string order = "big";
          if(a_it != a_end)
          {
            if(is_node_type(*a_it, "Constant"))
            {
              const jsont &v = json_member(*a_it, "value");
              if(v.is_string())
                order = json_string(v);
            }
          }

          // PLR §4.4.2: 'signed' is a kwarg-only parameter.
          // Default False. Accept either positional 'signed' as
          // a Constant (the third arg in some call forms — used
          // by the regression suite) or via 'keywords'.
          bool is_signed = false;
          auto eval_bool_constant = [](const jsont &v) -> bool
          {
            if(v.is_boolean())
              return v.is_true();
            // Fallback for textual encodings.
            return v.value == "True" || v.value == "true";
          };
          if(a_it != a_end && is_node_type(*a_it, "Constant"))
          {
            const jsont &v = json_member(*a_it, "value");
            is_signed = eval_bool_constant(v);
          }
          {
            const jsont &kws = json_member(expr, "keywords");
            if(kws.is_array())
            {
              for(const auto &kw : as_array(kws))
              {
                if(json_string(json_member(kw, "arg")) == "signed")
                {
                  const jsont &kvn = json_member(kw, "value");
                  if(is_node_type(kvn, "Constant"))
                    is_signed = eval_bool_constant(json_member(kvn, "value"));
                }
              }
            }
          }

          if(!is_python_list_type(b.type()))
          {
            return side_effect_expr_nondett{
              python_int_type(), get_location(expr)};
          }

          const typet i64 = python_int_type();
          member_exprt b_length{b, "length", i64};
          const auto &lt = to_struct_type(b.type());
          typet data_arr_type;
          for(const auto &c : lt.components())
            if(c.get_name() == "data")
              data_arr_type = c.type();
          member_exprt b_data{b, "data", data_arr_type};

          // Build a chain of conditional accumulations:
          //   result = (i < length) ? acc(result, b[i]) : result
          // For up to 16 bytes (more than fits any 64-bit int).
          const std::size_t max_bytes = 16;
          exprt result = from_integer(0, i64);
          if(order == "little")
          {
            // result += b.data[i] * 256^i  for i < length
            exprt power = from_integer(1, i64);
            for(std::size_t i = 0; i < max_bytes; i++)
            {
              exprt idx = from_integer(i, signedbv_typet{64});
              exprt byte = typecast_exprt{index_exprt{b_data, idx}, i64};
              exprt new_result = plus_exprt{result, mult_exprt{byte, power}};
              exprt cond =
                binary_relation_exprt{b_length, ID_gt, from_integer(i, i64)};
              result = if_exprt{cond, new_result, result};
              power = mult_exprt{power, from_integer(256, i64)};
            }
          }
          else
          {
            // big-endian: each step shifts left by 8 and
            // adds the next byte if i < length.
            for(std::size_t i = 0; i < max_bytes; i++)
            {
              exprt idx = from_integer(i, signedbv_typet{64});
              exprt byte = typecast_exprt{index_exprt{b_data, idx}, i64};
              exprt new_result =
                plus_exprt{mult_exprt{result, from_integer(256, i64)}, byte};
              exprt cond =
                binary_relation_exprt{b_length, ID_gt, from_integer(i, i64)};
              result = if_exprt{cond, new_result, result};
            }
          }
          // PLR §4.4.2: signed=True — interpret high bit of the
          // most-significant byte as the sign. If set, subtract
          // 2^(length*8) from the unsigned result. The
          // most-significant byte is b[0] for big-endian and
          // b[length-1] for little-endian.
          if(is_signed)
          {
            // Compute 2^(length*8) as a chain: power = 1, mul by
            // 256 'length' times.
            exprt total_bits = mult_exprt{b_length, from_integer(8, i64)};
            (void)total_bits;
            // Build a per-length chain: if length == k, sub_amount = 256^k
            exprt sub_amount = from_integer(0, i64);
            exprt power = from_integer(1, i64);
            for(std::size_t k = 0; k <= max_bytes; k++)
            {
              exprt match = equal_exprt{b_length, from_integer(k, i64)};
              sub_amount = if_exprt{match, power, sub_amount};
              power = mult_exprt{power, from_integer(256, i64)};
            }
            // Determine sign bit: high bit of MSB.
            exprt msb_idx;
            if(order == "little")
              msb_idx =
                minus_exprt{b_length, from_integer(1, signedbv_typet{64})};
            else
              msb_idx = from_integer(0, signedbv_typet{64});
            // Guard msb_idx >= 0 (length > 0). Empty bytes is ok
            // (sub_amount = 0 from chain).
            exprt msb_byte = typecast_exprt{index_exprt{b_data, msb_idx}, i64};
            exprt sign_bit_set = notequal_exprt{
              bitand_exprt{msb_byte, from_integer(0x80, i64)},
              from_integer(0, i64)};
            exprt has_bytes =
              binary_relation_exprt{b_length, ID_gt, from_integer(0, i64)};
            exprt is_neg = and_exprt{has_bytes, sign_bit_set};
            // result = is_neg ? result - sub_amount : result
            result = if_exprt{is_neg, minus_exprt{result, sub_amount}, result};
          }
          return result;
        }
      }
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
          if(func_name == "ceil" || func_name == "floor")
          {
            // PLib §math: floor/ceil of NaN raises ValueError, of
            // ±inf raises OverflowError. CBMC's typecast to int
            // produces an undefined-but-deterministic value for
            // these inputs, which silently swallows the bug.
            // Surface it as a property violation so callers see
            // FAIL instead of SUCCESS on NaN/inf inputs.
            add_check(
              not_exprt{or_exprt{isnan_exprt{math_arg}, isinf_exprt{math_arg}}},
              "exception",
              std::string{"ValueError: math."} + func_name +
                "() argument must be finite",
              get_location(expr));
          }
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
        // PLR §8.5: collections module — defaultdict / Counter
        // via module-qualified call, e.g. collections.defaultdict(int)
        // or col.defaultdict(int) when imported as col.
        if(
          imported_modules.count(obj_name) > 0 &&
          (method_name == "defaultdict" || method_name == "Counter"))
        {
          std::string factory;
          if(method_name == "Counter")
            factory = "int";
          else if(args.is_array() && !as_array(args).empty())
          {
            const jsont &fa = *as_array(args).begin();
            if(is_node_type(fa, "Name"))
              factory = json_string(json_member(fa, "id"));
            else if(is_node_type(fa, "Constant"))
            {
              const jsont &cv = json_member(fa, "value");
              if(cv.is_null())
                factory = "";
            }
          }
          typet val_type = python_int_type();
          if(factory == "str")
            val_type = python_string_type();
          else if(factory == "list")
            val_type = python_list_type(python_int_type());
          else if(factory == "float")
            val_type = double_type();
          typet dict_type = python_dict_type(python_string_type(), val_type);
          exprt empty = safe_zero(dict_type);
          pending_defaultdict_factory = factory;
          return empty;
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
          // PLib stdtypes: str.split(sep[, maxsplit]) — constant optimization
          if(args.is_array() && !as_array(args).empty())
          {
            auto ait = as_array(args).begin();
            exprt delim_expr = convert_expression(*ait);
            // PLR: optional 2nd arg is maxsplit (max number of
            // splits performed; default unlimited = -1).
            long long max_split = -1;
            ++ait;
            if(ait != as_array(args).end())
            {
              auto cv = try_eval_double(convert_expression(*ait));
              if(cv.has_value())
                max_split = static_cast<long long>(cv.value());
            }
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
                long long splits = 0;
                while(pos <= s.size())
                {
                  if(max_split >= 0 && splits >= max_split)
                  {
                    parts.push_back(s.substr(pos));
                    break;
                  }
                  auto found = s.find(d, pos);
                  if(found == std::string::npos)
                  {
                    parts.push_back(s.substr(pos));
                    break;
                  }
                  parts.push_back(s.substr(pos, found - pos));
                  pos = found + d.size();
                  splits++;
                }
              }
              else
                parts.push_back(s);

              typet str_type = python_string_type();
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
                  typet str_type = python_string_type();
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
          if(
            !sv.has_value() &&
            (obj.id() == ID_symbol || obj.id() == ID_dereference))
          {
            auto it = string_constants.find(
              obj.id() == ID_symbol ? to_symbol_expr(obj).get_identifier()
                                    : irep_idt{});
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
              pending_checks,
              loop_depth > 0);
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
        if(
          method_name == "replace" || method_name == "format" ||
          method_name == "format_map")
        {
          // PLib stdtypes: str.replace(old, new[, count]) for constant strings
          if(
            method_name == "replace" && args.is_array() &&
            as_array(args).size() >= 2)
          {
            auto ait = as_array(args).begin();
            exprt old_expr = convert_expression(*ait);
            ++ait;
            exprt new_expr = convert_expression(*ait);
            // PLR: optional 3rd arg is the maximum number of
            // replacements; default is 'unlimited'. -1 sentinel.
            long long max_count = -1;
            ++ait;
            if(ait != as_array(args).end())
            {
              auto cv = try_eval_double(convert_expression(*ait));
              if(cv.has_value())
                max_count = static_cast<long long>(cv.value());
            }
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
              // Perform replacement up to max_count times.
              std::string result;
              std::size_t pos = 0;
              long long replaced = 0;
              while(pos < src.size())
              {
                if(max_count >= 0 && replaced >= max_count)
                {
                  result += src.substr(pos);
                  break;
                }
                auto found = src.find(old_s, pos);
                if(found == std::string::npos)
                {
                  result += src.substr(pos);
                  break;
                }
                result += src.substr(pos, found - pos) + new_s;
                pos = found + old_s.size();
                replaced++;
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
          // PLib stdtypes: str.format_map(mapping) — raise
          // KeyError when a {name} placeholder isn't a key in
          // the mapping. We handle the common case where the
          // mapping argument is a dict literal whose keys are
          // resolvable at conversion time.
          if(method_name == "format_map" && args.is_array())
          {
            auto fmt_sv = extract_string_value(obj);
            if(fmt_sv.has_value())
            {
              std::string fmt = fmt_sv.value();
              // Collect named placeholders.
              std::set<std::string> placeholders;
              for(std::size_t i = 0; i + 1 < fmt.size(); i++)
              {
                if(fmt[i] != '{' || fmt[i + 1] == '{')
                  continue;
                auto close = fmt.find('}', i + 1);
                if(close == std::string::npos)
                  continue;
                std::string spec = fmt.substr(i + 1, close - i - 1);
                if(spec.empty() || std::isdigit(spec[0]) || spec[0] == ':')
                {
                  i = close;
                  continue;
                }
                auto colon = spec.find(':');
                std::string name =
                  colon == std::string::npos ? spec : spec.substr(0, colon);
                placeholders.insert(name);
                i = close;
              }
              // Resolve the mapping keys AND values when it's a
              // literal. We capture each (key, AST-value) pair so
              // we can substitute below.
              std::set<std::string> mapping_keys;
              std::map<std::string, jsont> mapping_pairs;
              bool keys_known = false;
              auto a_it = as_array(args).begin();
              if(a_it != as_array(args).end())
              {
                if(is_node_type(*a_it, "Dict"))
                {
                  keys_known = true;
                  const jsont &kn = json_member(*a_it, "keys");
                  const jsont &vn = json_member(*a_it, "values");
                  if(kn.is_array() && vn.is_array())
                  {
                    auto ki = as_array(kn).begin();
                    auto vi = as_array(vn).begin();
                    for(; ki != as_array(kn).end() && vi != as_array(vn).end();
                        ++ki, ++vi)
                    {
                      if(is_node_type(*ki, "Constant"))
                      {
                        const jsont &v = json_member(*ki, "value");
                        if(v.is_string())
                        {
                          mapping_keys.insert(v.value);
                          mapping_pairs[v.value] = *vi;
                        }
                      }
                    }
                  }
                }
              }
              if(keys_known)
              {
                bool missing = false;
                for(const auto &name : placeholders)
                {
                  if(mapping_keys.count(name) == 0)
                  {
                    missing = true;
                    const symbolt *exc_sym =
                      symbol_table.lookup("python::__exception_active");
                    const symbolt *exc_type_sym =
                      symbol_table.lookup("python::__exception_type");
                    if(exc_sym != nullptr)
                    {
                      pending_checks.push_back(code_frontend_assignt{
                        exc_sym->symbol_expr(), true_exprt{}});
                      if(exc_type_sym != nullptr)
                      {
                        long h = exception_type_hash("KeyError");
                        pending_checks.push_back(code_frontend_assignt{
                          exc_type_sym->symbol_expr(),
                          from_integer(h, python_int_type())});
                      }
                    }
                    break;
                  }
                }
                if(!missing)
                {
                  // PLR §6.1.4: substitute every {name} placeholder
                  // with str(mapping[name]) when both the format
                  // string and all referenced values are constants.
                  std::string result;
                  bool all_const = true;
                  for(std::size_t i = 0; i < fmt.size(); i++)
                  {
                    if(fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '{')
                    {
                      result += '{';
                      ++i;
                      continue;
                    }
                    if(fmt[i] == '}' && i + 1 < fmt.size() && fmt[i + 1] == '}')
                    {
                      result += '}';
                      ++i;
                      continue;
                    }
                    if(fmt[i] == '{' && i + 1 < fmt.size())
                    {
                      auto close = fmt.find('}', i + 1);
                      if(close == std::string::npos)
                      {
                        result += fmt[i];
                        continue;
                      }
                      std::string spec = fmt.substr(i + 1, close - i - 1);
                      i = close;
                      auto colon = spec.find(':');
                      std::string name = colon == std::string::npos
                                           ? spec
                                           : spec.substr(0, colon);
                      auto pi = mapping_pairs.find(name);
                      if(pi == mapping_pairs.end())
                      {
                        all_const = false;
                        break;
                      }
                      const jsont &val_ast = pi->second;
                      if(is_node_type(val_ast, "Constant"))
                      {
                        const jsont &v = json_member(val_ast, "value");
                        if(v.is_string())
                          result += v.value;
                        else if(v.is_number())
                        {
                          // e.g. "{x}".format_map({"x": 1}) -> "1"
                          double d = std::stod(v.value);
                          if(d == std::floor(d) && std::abs(d) < 1e15)
                            result += std::to_string(static_cast<long long>(d));
                          else
                            result += v.value;
                        }
                        else if(v.is_true())
                          result += "True";
                        else if(v.is_false())
                          result += "False";
                        else
                          all_const = false;
                      }
                      else
                        all_const = false;
                    }
                    else
                      result += fmt[i];
                  }
                  if(all_const)
                    return python_string_literal(result);
                }
              }
            }
            return side_effect_expr_nondett{
              python_string_type(), get_location(expr)};
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
                // PLR §6.1.3: '{{' is an escape for a literal '{',
                // and '}}' is an escape for a literal '}'.
                if(fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '{')
                {
                  result += '{';
                  ++i;
                  continue;
                }
                if(fmt[i] == '}' && i + 1 < fmt.size() && fmt[i + 1] == '}')
                {
                  result += '}';
                  ++i;
                  continue;
                }
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
                    // PLR §6.1.4: format with a named placeholder
                    // like '{name}'. If no keyword arg with this
                    // name is provided, raise KeyError. We check
                    // the call's 'keywords' field for a matching
                    // arg name; missing → KeyError.
                    std::string key_name = spec;
                    auto colon_pos = key_name.find(':');
                    if(colon_pos != std::string::npos)
                      key_name = key_name.substr(0, colon_pos);
                    bool found_kw = false;
                    const jsont &kws = json_member(expr, "keywords");
                    if(kws.is_array())
                    {
                      for(const auto &kw : as_array(kws))
                      {
                        if(json_string(json_member(kw, "arg")) == key_name)
                        {
                          found_kw = true;
                          break;
                        }
                      }
                    }
                    if(!found_kw)
                    {
                      const symbolt *exc_sym =
                        symbol_table.lookup("python::__exception_active");
                      const symbolt *exc_type_sym =
                        symbol_table.lookup("python::__exception_type");
                      if(exc_sym != nullptr)
                      {
                        pending_checks.push_back(code_frontend_assignt{
                          exc_sym->symbol_expr(), true_exprt{}});
                        if(exc_type_sym != nullptr)
                        {
                          long h = exception_type_hash("KeyError");
                          pending_checks.push_back(code_frontend_assignt{
                            exc_type_sym->symbol_expr(),
                            from_integer(h, python_int_type())});
                        }
                      }
                    }
                    all_const = false;
                    continue;
                  }

                  if(use_idx >= arg_exprs.size())
                  {
                    // PLR §6.1.4: format() raises IndexError
                    // when a positional placeholder index has
                    // no matching argument (e.g. '{1}'.format('a')
                    // or '{} {}'.format('a')).
                    const symbolt *exc_sym =
                      symbol_table.lookup("python::__exception_active");
                    const symbolt *exc_type_sym =
                      symbol_table.lookup("python::__exception_type");
                    if(exc_sym != nullptr)
                    {
                      pending_checks.push_back(code_frontend_assignt{
                        exc_sym->symbol_expr(), true_exprt{}});
                      if(exc_type_sym != nullptr)
                      {
                        long h = exception_type_hash("IndexError");
                        pending_checks.push_back(code_frontend_assignt{
                          exc_type_sym->symbol_expr(),
                          from_integer(h, python_int_type())});
                      }
                    }
                    all_const = false;
                    continue;
                  }

                  // Extract value
                  auto sv = extract_string_value(arg_exprs[use_idx]);
                  if(sv.has_value())
                  {
                    result += sv.value();
                  }
                  else if(
                    arg_exprs[use_idx].type().id() == ID_bool &&
                    arg_exprs[use_idx].is_constant())
                  {
                    // PLR §6.1.4: format() of a bool yields its
                    // str() spelling ("True" / "False"), not the
                    // numeric value 1 / 0.
                    bool bv = to_constant_expr(arg_exprs[use_idx]).is_true();
                    result += bv ? "True" : "False";
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
                  pending_checks,
                  loop_depth > 0);
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
        // PLib stdtypes: startswith/endswith — constant fast path.
        // For non-constant string operands the byte-by-byte
        // comparison via member_exprt(.., "data", array_typet)
        // type-confused the field (the actual struct field is a
        // pointer, not an inline array), producing
        // `(select <pointer-bv> <idx>)` SMT — rejected by cvc5
        // with "array select operating on non-array". The
        // semantically-correct refinement-string path is taken
        // by the second startswith/endswith handler below
        // (line ~1693), which emits
        // cprover_string_is_prefix_func / is_suffix_func.
        if(
          (method_name == "startswith" || method_name == "endswith") &&
          args.is_array() && !as_array(args).empty())
        {
          exprt prefix = convert_expression(*as_array(args).begin());
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
          // Non-constant case: fall through to refinement-string path.
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
          // Non-constant string predicate: return nondet bool
          // (the predicate's actual type — was python_string by
          // mistake, which forced downstream truthy-conversion
          // to compare struct.length != 0 and mis-evaluate).
          return side_effect_expr_nondett{bool_typet{}, source_locationt{}};
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
          if(
            !str_val.has_value() &&
            (obj.id() == ID_symbol || obj.id() == ID_dereference))
          {
            auto it = string_constants.find(
              obj.id() == ID_symbol ? to_symbol_expr(obj).get_identifier()
                                    : irep_idt{});
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
                // PLR: str.index(sub, start, end) searches the
                // substring s[start:end], NOT the full string.
                auto pos = (method_name == "index") ? slice.find(sub)
                                                    : slice.rfind(sub);
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
                // Translate slice-relative position back to the
                // original-string position.
                return from_integer(
                  static_cast<long long>(pos) +
                    static_cast<long long>(start),
                  python_int_type());
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

            // Default value: None if not specified, else second arg.
            // The Python-int "None sentinel" is only representable in an
            // integer-typed value column; for bool/float/string/etc. dicts
            // there is no in-band None and we fall back to safe_zero of
            // the element type (e.g. False for bool, 0.0 for float).
            // Without this guard, from_integer aborts on the precondition
            // when the value type cannot hold the sentinel — see
            // regression test cbmc/python-dict-get-bool.
            exprt default_val;
            {
              const typet &elem_t = vals_type.element_type();
              if(
                elem_t.id() == ID_signedbv || elem_t.id() == ID_unsignedbv ||
                elem_t.id() == ID_integer || elem_t.id() == ID_natural)
                default_val =
                  from_integer(mp_integer{-4611686018427387904LL}, elem_t);
              else
                default_val = safe_zero(elem_t);
            }
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
              else if((obj.id() == ID_symbol || obj.id() == ID_dereference))
              {
                auto it = dict_literals.find(
                  obj.id() == ID_symbol ? to_symbol_expr(obj).get_identifier()
                                        : irep_idt{});
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
          else if((obj.id() == ID_symbol || obj.id() == ID_dereference))
          {
            auto it = dict_literals.find(
              obj.id() == ID_symbol ? to_symbol_expr(obj).get_identifier()
                                    : irep_idt{});
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
          else if((obj.id() == ID_symbol || obj.id() == ID_dereference))
          {
            auto it = dict_literals.find(
              obj.id() == ID_symbol ? to_symbol_expr(obj).get_identifier()
                                    : irep_idt{});
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
          else if((obj.id() == ID_symbol || obj.id() == ID_dereference))
          {
            auto it = dict_literals.find(
              obj.id() == ID_symbol ? to_symbol_expr(obj).get_identifier()
                                    : irep_idt{});
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
          // Returns list of tuples — for non-literal dicts,
          // construct a list whose i-th element is the tuple
          // (obj.keys[i], obj.values[i]). The for-loop walks
          // the resulting list using its own counter, so
          // unwinds naturally bound by the dict's actual length.
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

            member_exprt obj_keys{obj, "keys", keys_type};
            member_exprt obj_vals{obj, "values", vals_type};
            member_exprt obj_len{obj, "length", signedbv_typet{64}};

            exprt::operandst elems;
            for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; ++i)
            {
              exprt idx = from_integer(i, signedbv_typet{64});
              elems.push_back(struct_exprt{
                {index_exprt{obj_keys, idx}, index_exprt{obj_vals, idx}},
                tuple_t});
            }
            return struct_exprt{
              {obj_len, array_exprt{std::move(elems), list_data_type}}, list_t};
          }
        }
        if(method_name == "clear")
        {
          // d.clear() → set d.length = 0
          if((obj.id() == ID_symbol || obj.id() == ID_dereference))
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
                if((obj.id() == ID_symbol || obj.id() == ID_dereference))
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
          method_name == "setdefault" &&
          (obj.id() == ID_symbol || obj.id() == ID_dereference))
        {
          // PLR dict.setdefault(k, default=None): if k is in d
          // return d[k]; otherwise insert (k, default) and return
          // default. We model this with a side-effecting dict
          // store on the missing-key path so a later `k in d`
          // check correctly reports True.
          const auto &dict_st = to_struct_type(obj_base_type);
          const auto &keys_type =
            to_array_type(dict_st.components()[1].type());
          const auto &vals_type =
            to_array_type(dict_st.components()[2].type());
          member_exprt length{obj, "length", signedbv_typet{64}};
          member_exprt keys_arr{obj, "keys", keys_type};
          member_exprt vals_arr{obj, "values", vals_type};

          // Resolve key (1st arg) and default (2nd arg or
          // safe_zero of the value-type — which loses the
          // None-ness when value-type can't hold the sentinel,
          // but matches what dict.get already does).
          if(!args.is_array() || as_array(args).empty())
            return side_effect_expr_nondett{
              vals_type.element_type(), get_location(expr)};
          auto arg_it = as_array(args).begin();
          exprt key_expr = convert_expression(*arg_it);
          if(key_expr.type() != keys_type.element_type())
            key_expr = safe_typecast(key_expr, keys_type.element_type());
          exprt default_val;
          {
            const typet &elem_t = vals_type.element_type();
            if(
              elem_t.id() == ID_signedbv ||
              elem_t.id() == ID_unsignedbv ||
              elem_t.id() == ID_integer || elem_t.id() == ID_natural)
              default_val =
                from_integer(mp_integer{-4611686018427387904LL}, elem_t);
            else
              default_val = safe_zero(elem_t);
          }
          ++arg_it;
          if(arg_it != as_array(args).end())
          {
            default_val = convert_expression(*arg_it);
            if(default_val.type() != vals_type.element_type())
              default_val =
                safe_typecast(default_val, vals_type.element_type());
          }

          // Allocate found flag + result temp.
          static unsigned sd_ctr = 0;
          std::string fn = "__sd_found_" + std::to_string(sd_ctr++);
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
          std::string rn = "__sd_result_" + std::to_string(sd_ctr - 1);
          std::string rq = qualify_name(rn);
          irep_idt ri{rq};
          if(symbol_table.lookup(ri) == nullptr)
          {
            symbolt rs{ri, vals_type.element_type(), "python"};
            rs.base_name = rn;
            rs.is_lvalue = true;
            rs.is_state_var = true;
            symbol_table.add(rs);
          }
          symbol_exprt result = symbol_table.lookup_ref(ri).symbol_expr();

          pending_checks.push_back(code_frontend_assignt{found, false_exprt{}});
          pending_checks.push_back(code_frontend_assignt{result, default_val});
          for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
          {
            exprt idx = from_integer(i, signedbv_typet{64});
            exprt in_range = binary_relation_exprt{idx, ID_lt, length};
            exprt match = equal_exprt{index_exprt{keys_arr, idx}, key_expr};
            code_blockt update;
            update.add(code_frontend_assignt{found, true_exprt{}});
            update.add(
              code_frontend_assignt{result, index_exprt{vals_arr, idx}});
            pending_checks.push_back(
              code_ifthenelset{and_exprt{in_range, match}, std::move(update)});
          }
          // If not found, append (key, default) and set length+=1.
          code_blockt append;
          append.add(
            code_frontend_assignt{index_exprt{keys_arr, length}, key_expr});
          append.add(
            code_frontend_assignt{index_exprt{vals_arr, length}, default_val});
          append.add(code_frontend_assignt{
            length, plus_exprt{length, from_integer(1, signedbv_typet{64})}});
          pending_checks.push_back(
            code_ifthenelset{not_exprt{found}, std::move(append)});

          // Invalidate the compile-time dict-literal tracking for
          // this symbol. dict_literals captures the literal value of
          // the dict at construction time and is used by the `key in
          // dict` constant-fold path; without invalidation the fold
          // would report the pre-setdefault state and miss the key
          // we just inserted.
          if(obj.id() == ID_symbol)
            dict_literals.erase(to_symbol_expr(obj).get_identifier());

          return result;
        }
        if(
          method_name == "pop" &&
          (obj.id() == ID_symbol || obj.id() == ID_dereference))
        {
          // PLR dict.pop(key, default=...): if key in d, remove
          // and return its value. If key not in d and a default
          // is supplied, return default. If key not in d and no
          // default is supplied, raise KeyError.
          const auto &dict_st = to_struct_type(obj_base_type);
          const auto &keys_type =
            to_array_type(dict_st.components()[1].type());
          const auto &vals_type =
            to_array_type(dict_st.components()[2].type());
          member_exprt length{obj, "length", signedbv_typet{64}};
          member_exprt keys_arr{obj, "keys", keys_type};
          member_exprt vals_arr{obj, "values", vals_type};

          if(!args.is_array() || as_array(args).empty())
            return side_effect_expr_nondett{
              vals_type.element_type(), get_location(expr)};
          auto arg_it = as_array(args).begin();
          exprt key_expr = convert_expression(*arg_it);
          if(key_expr.type() != keys_type.element_type())
            key_expr = safe_typecast(key_expr, keys_type.element_type());
          bool has_default = false;
          exprt default_val = safe_zero(vals_type.element_type());
          ++arg_it;
          if(arg_it != as_array(args).end())
          {
            has_default = true;
            default_val = convert_expression(*arg_it);
            if(default_val.type() != vals_type.element_type())
              default_val =
                safe_typecast(default_val, vals_type.element_type());
          }

          static unsigned pop_ctr = 0;
          std::string fn = "__pop_found_" + std::to_string(pop_ctr++);
          irep_idt fi{qualify_name(fn)};
          if(symbol_table.lookup(fi) == nullptr)
          {
            symbolt fs{fi, bool_typet{}, "python"};
            fs.base_name = fn;
            fs.is_lvalue = true;
            fs.is_state_var = true;
            symbol_table.add(fs);
          }
          symbol_exprt found = symbol_table.lookup_ref(fi).symbol_expr();

          std::string rn = "__pop_result_" + std::to_string(pop_ctr - 1);
          irep_idt ri{qualify_name(rn)};
          if(symbol_table.lookup(ri) == nullptr)
          {
            symbolt rs{ri, vals_type.element_type(), "python"};
            rs.base_name = rn;
            rs.is_lvalue = true;
            rs.is_state_var = true;
            symbol_table.add(rs);
          }
          symbol_exprt result = symbol_table.lookup_ref(ri).symbol_expr();

          pending_checks.push_back(code_frontend_assignt{found, false_exprt{}});
          pending_checks.push_back(code_frontend_assignt{result, default_val});
          // Find and remove (compact by shifting).
          for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
          {
            exprt idx = from_integer(i, signedbv_typet{64});
            exprt in_range = binary_relation_exprt{idx, ID_lt, length};
            exprt match = equal_exprt{index_exprt{keys_arr, idx}, key_expr};
            code_blockt update;
            update.add(code_frontend_assignt{found, true_exprt{}});
            update.add(
              code_frontend_assignt{result, index_exprt{vals_arr, idx}});
            // Shift remaining entries down to compact.
            for(std::size_t j = i; j + 1 < PYTHON_MAX_DICT_SIZE; j++)
            {
              exprt jdx = from_integer(j, signedbv_typet{64});
              exprt jdx1 = from_integer(j + 1, signedbv_typet{64});
              update.add(code_frontend_assignt{
                index_exprt{keys_arr, jdx}, index_exprt{keys_arr, jdx1}});
              update.add(code_frontend_assignt{
                index_exprt{vals_arr, jdx}, index_exprt{vals_arr, jdx1}});
            }
            update.add(code_frontend_assignt{
              length, minus_exprt{length, from_integer(1, signedbv_typet{64})}});
            pending_checks.push_back(
              code_ifthenelset{and_exprt{in_range, match}, std::move(update)});
          }
          // KeyError when not found and no default given.
          if(!has_default)
          {
            add_check(
              found,
              "exception",
              "KeyError: key not found in dict",
              get_location(expr));
          }
          if(obj.id() == ID_symbol)
            dict_literals.erase(to_symbol_expr(obj).get_identifier());
          return result;
        }
        if(
          method_name == "popitem" &&
          (obj.id() == ID_symbol || obj.id() == ID_dereference))
        {
          // PLR dict.popitem(): remove and return an arbitrary
          // (key, value) pair. Raises KeyError on empty dict.
          const auto &dict_st = to_struct_type(obj_base_type);
          const auto &keys_type =
            to_array_type(dict_st.components()[1].type());
          const auto &vals_type =
            to_array_type(dict_st.components()[2].type());
          member_exprt length{obj, "length", signedbv_typet{64}};
          // Empty-dict KeyError check.
          add_check(
            binary_relation_exprt{
              length, ID_gt, from_integer(0, signedbv_typet{64})},
            "exception",
            "KeyError: dictionary is empty",
            get_location(expr));
          // Snapshot the pre-popitem length so the (key, value) we
          // return doesn't depend on the post-decrement length.
          // pending_checks are emitted BEFORE the surrounding
          // statement; without this snapshot the decrement would
          // race ahead of the read and we'd return keys[length-2].
          static unsigned pi_ctr = 0;
          std::string ln = "__popitem_len_" + std::to_string(pi_ctr++);
          irep_idt li{qualify_name(ln)};
          if(symbol_table.lookup(li) == nullptr)
          {
            symbolt ls{li, signedbv_typet{64}, "python"};
            ls.base_name = ln;
            ls.is_lvalue = true;
            ls.is_state_var = true;
            symbol_table.add(ls);
          }
          symbol_exprt last_idx_sym =
            symbol_table.lookup_ref(li).symbol_expr();
          pending_checks.push_back(code_frontend_assignt{
            last_idx_sym,
            minus_exprt{length, from_integer(1, signedbv_typet{64})}});
          // Return tuple (keys[snapshot], values[snapshot]) since
          // CPython pops in LIFO order.
          member_exprt keys_arr{obj, "keys", keys_type};
          member_exprt vals_arr{obj, "values", vals_type};
          exprt key_at = index_exprt{keys_arr, last_idx_sym};
          exprt val_at = index_exprt{vals_arr, last_idx_sym};
          struct_typet::componentst tcomps;
          tcomps.push_back(struct_typet::componentt{"_0", key_at.type()});
          tcomps.push_back(struct_typet::componentt{"_1", val_at.type()});
          struct_typet tuple_t{tcomps};
          tuple_t.set_tag("python_tuple");
          // Decrement length AFTER recording the snapshot.
          pending_checks.push_back(
            code_frontend_assignt{length, last_idx_sym});
          if(obj.id() == ID_symbol)
            dict_literals.erase(to_symbol_expr(obj).get_identifier());
          return struct_exprt{{key_at, val_at}, tuple_t};
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
        // PLR §3.3.1: list.__iter__() returns the list itself,
        // which is sufficient for our list-as-iterator model.
        // The for-loop iter path expects the same shape and
        // walks .data[0..length-1] via its own counter.
        if(method_name == "__iter__")
          return obj;
        // PLR §4.6: bytes are modelled as list[uint8]; expose
        // bytes.decode(encoding) by repackaging the bytes' data
        // pointer as a python_string struct. We don't translate
        // multibyte encodings — for ascii / latin-1 / utf-8 of
        // ASCII-only content the byte pattern is the same; for
        // other content the verifier sees the raw bytes which
        // are still sound for length and indexing checks.
        if(method_name == "decode")
        {
          const auto &list_st = to_struct_type(obj_base_type);
          const auto &data_type = to_array_type(list_st.components()[1].type());
          if(
            data_type.element_type().id() == ID_unsignedbv &&
            to_unsignedbv_type(data_type.element_type()).get_width() == 8)
          {
            // PLR §4.6: if the bytes value is a compile-time
            // constant (struct_exprt with constant length and
            // data array), recover the content and return a
            // python_string literal so subsequent string-solver
            // operations have a known content. This covers
            // 'b"hello".decode("utf-8")' and 'BS.decode(...)'
            // when BS = b"...".
            const exprt *src = nullptr;
            if(
              obj.id() == ID_struct && obj.operands().size() >= 2 &&
              obj.operands()[0].is_constant() &&
              obj.operands()[1].id() == ID_array)
              src = &obj;
            else if(obj.id() == ID_symbol)
            {
              auto it =
                list_literals.find(to_symbol_expr(obj).get_identifier());
              if(it != list_literals.end())
                src = &it->second;
            }
            if(
              src != nullptr && src->operands().size() >= 2 &&
              src->operands()[0].is_constant() &&
              src->operands()[1].id() == ID_array)
            {
              mp_integer blen;
              if(!to_integer(to_constant_expr(src->operands()[0]), blen))
              {
                std::string content;
                const exprt &data_arr = src->operands()[1];
                std::size_t n = std::min<std::size_t>(
                  blen.to_ulong(), data_arr.operands().size());
                for(std::size_t i = 0; i < n; i++)
                {
                  mp_integer bv;
                  if(!to_integer(to_constant_expr(data_arr.operands()[i]), bv))
                    content.push_back(static_cast<char>(bv.to_ulong()));
                }
                return python_string_literal(content);
              }
            }
            // Runtime bytes: repackage data pointer as a
            // python_string. Sound for length and indexing
            // queries; the string solver may still produce
            // nondet content because there's no explicit
            // array-to-pointer association.
            member_exprt blen{obj, "length", signedbv_typet{64}};
            member_exprt bdata{obj, "data", data_type};
            exprt data_ptr = address_of_exprt{
              index_exprt{bdata, from_integer(0, signedbv_typet{64})}};
            return struct_exprt{{blen, data_ptr}, python_string_type()};
          }
        }
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
          // PLR §6.10: when the list is a known literal whose
          // elements are all constant ints or constant strings,
          // sort at conversion time and rewrite the list. Avoids
          // the O(n²) bubble-sort below which would emit one
          // string-solver comparison per pass per pair and time
          // out on string lists.
          const exprt *lit = nullptr;
          irep_idt list_sym;
          if(obj.id() == ID_symbol)
          {
            list_sym = to_symbol_expr(obj).get_identifier();
            auto it = list_literals.find(list_sym);
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
              std::vector<std::pair<mp_integer, exprt>> int_pairs;
              std::vector<std::pair<std::string, exprt>> str_pairs;
              bool all_const_int = true;
              bool all_const_str = true;
              for(mp_integer i = 0; i < lv; ++i)
              {
                auto idx = i.to_ulong();
                if(idx >= data_arr.operands().size())
                {
                  all_const_int = false;
                  all_const_str = false;
                  break;
                }
                const exprt &e = data_arr.operands()[idx];
                if(all_const_int)
                {
                  if(!e.is_constant() || e.type().id() != ID_signedbv)
                    all_const_int = false;
                  else
                  {
                    mp_integer val;
                    if(to_integer(to_constant_expr(e), val))
                      all_const_int = false;
                    else
                      int_pairs.emplace_back(val, e);
                  }
                }
                if(all_const_str)
                {
                  auto sv = extract_string_value(e);
                  if(!sv.has_value())
                    all_const_str = false;
                  else
                    str_pairs.emplace_back(sv.value(), e);
                }
              }
              if(
                (all_const_int && !int_pairs.empty()) ||
                (all_const_str && !str_pairs.empty()))
              {
                exprt::operandst sorted_elems;
                if(all_const_int)
                {
                  std::sort(
                    int_pairs.begin(),
                    int_pairs.end(),
                    [](const auto &a, const auto &b)
                    { return a.first < b.first; });
                  for(const auto &p : int_pairs)
                    sorted_elems.push_back(p.second);
                }
                else
                {
                  std::sort(
                    str_pairs.begin(),
                    str_pairs.end(),
                    [](const auto &a, const auto &b)
                    { return a.first < b.first; });
                  for(const auto &p : str_pairs)
                    sorted_elems.push_back(p.second);
                }
                while(sorted_elems.size() < PYTHON_MAX_LIST_LENGTH)
                  sorted_elems.push_back(safe_zero(data_type.element_type()));
                exprt sorted_struct = struct_exprt{
                  {lit->operands()[0],
                   array_exprt{std::move(sorted_elems), data_type}},
                  obj.type()};
                pending_checks.push_back(
                  code_frontend_assignt{obj, sorted_struct});
                if(!list_sym.empty())
                  list_literals[list_sym] = sorted_struct;
                return from_integer(0, python_int_type()); // None
              }
            }
          }
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

          // PLR list.pop: raises IndexError when the list is empty
          // (default pop() with length == 0) or when the supplied
          // index is out of range. Python also accepts negative
          // indices that wrap from the end (-1 == last); the valid
          // range after wrapping is [-length, length).
          {
            exprt zero64 = from_integer(0, signedbv_typet{64});
            // If pop_idx is non-negative: pop_idx < length.
            // If pop_idx is negative: pop_idx >= -length, i.e.
            //   pop_idx + length >= 0.
            exprt nonneg_ok = and_exprt{
              binary_relation_exprt{pop_idx, ID_ge, zero64},
              binary_relation_exprt{pop_idx, ID_lt, length}};
            exprt neg_ok = and_exprt{
              binary_relation_exprt{pop_idx, ID_lt, zero64},
              binary_relation_exprt{
                plus_exprt{pop_idx, length}, ID_ge, zero64}};
            add_check(
              or_exprt{nonneg_ok, neg_ok},
              "exception",
              "IndexError: pop index out of range",
              get_location(expr));
          }

          // Normalise negative index for the actual extraction.
          exprt zero64 = from_integer(0, signedbv_typet{64});
          pop_idx = if_exprt{
            binary_relation_exprt{pop_idx, ID_lt, zero64},
            plus_exprt{pop_idx, length},
            pop_idx};

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

        // PLib stdtypes: list.copy().
        // PLR §6.10.4: must produce a fresh list. Returning
        // `obj` directly aliases — subsequent mutations on the
        // chained result (e.g. `x.copy().append(99)`) would
        // mutate the original `x`. Allocate a temp symbol,
        // assign a struct-copy of obj to it, and return the
        // temp's symbol_expr. The caller chain then targets
        // the temp's storage.
        if(method_name == "copy")
        {
          static unsigned copy_ctr = 0;
          std::string tn = "__list_copy_" + std::to_string(copy_ctr++);
          std::string tq = qualify_name(tn);
          irep_idt ti{tq};
          if(symbol_table.lookup(ti) == nullptr)
          {
            symbolt ts{ti, obj.type(), "python"};
            ts.base_name = tn;
            ts.is_lvalue = true;
            ts.is_state_var = true;
            ts.is_static_lifetime = current_function.empty();
            symbol_table.add(ts);
          }
          symbol_exprt tmp = symbol_table.lookup_ref(ti).symbol_expr();
          pending_checks.push_back(code_frontend_assignt{tmp, obj});
          return tmp;
        }
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
              {
                // PLR §3.1: emit annotation-mismatch property
                // for method-call args, paralleling the
                // function-call path. Skip self (i == 0 with a
                // pointer param) so calling Foo's method on a
                // Foo instance doesn't fire.
                bool is_self = i == 0 && mparams[i].type().id() == ID_pointer;
                if(
                  !is_self && python_check_annotations &&
                  annotation_types_incompatible(
                    mparams[i].type(), arguments[i].type()))
                {
                  add_check(
                    false_exprt{},
                    "annotation-mismatch",
                    "argument " + std::to_string(i) +
                      "'s type does not match declared parameter type of '" +
                      method_name + "'",
                    get_location(expr));
                }
                arguments[i] = safe_typecast(arguments[i], mparams[i].type());
              }
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
              {
                // PLR §3.1: emit annotation-mismatch property
                // for method-call args that don't match
                // declared parameter types. Skip self.
                bool is_self = i == 0 && fp[i].type().id() == ID_pointer;
                if(
                  !is_self && python_check_annotations &&
                  annotation_types_incompatible(
                    fp[i].type(), arguments[i].type()))
                {
                  add_check(
                    false_exprt{},
                    "annotation-mismatch",
                    "argument " + std::to_string(i) +
                      "'s type does not match declared parameter type of '" +
                      method_name + "'",
                    get_location(expr));
                }
                arguments[i] = safe_typecast(arguments[i], fp[i].type());
              }
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
  // Verification-primitive dispatch group (nondet_*, randint,
  // assume family, __cbmc_re_* regex hooks). Extracted to
  // python_converter_call_nondet.cpp for clarity.
  if(auto r = try_nondet_call(expr, func_name, args))
    return std::move(*r);
  // Built-in free-function dispatch group (map, len, range,
  // sorted, isinstance, type, min, max, ...). Extracted to
  // python_converter_call_builtins.cpp for clarity.
  if(auto r = try_builtin_call(expr, func_name, args))
    return std::move(*r);

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

  // PLR §4.2.1: nested function definitions live in their
  // enclosing function's scope. Look up the callee in the
  // current function scope first, then walk outward through
  // enclosing_functions, then fall back to module scope.
  // This lets two sibling functions each define `def f(...)`
  // without colliding (the symbol id encodes the parent
  // chain — see python_converter_defs.cpp).
  auto try_function_scope = [&](const std::string &scope) -> const symbolt *
  {
    irep_idt sid{
      scope.empty() ? std::string{"python::" + func_name}
                    : std::string{"python::" + scope + "::" + func_name}};
    const symbolt *s = symbol_table.lookup(sid);
    if(s != nullptr && s->type.id() == ID_code)
      return s;
    return nullptr;
  };
  irep_idt symbol_id;
  const symbolt *sym = nullptr;
  if(!current_function.empty())
  {
    if((sym = try_function_scope(current_function)) != nullptr)
      symbol_id = irep_idt{"python::" + current_function + "::" + func_name};
  }
  if(sym == nullptr)
  {
    for(auto it = enclosing_functions.rbegin();
        sym == nullptr && it != enclosing_functions.rend();
        ++it)
    {
      if((sym = try_function_scope(*it)) != nullptr)
        symbol_id = irep_idt{"python::" + *it + "::" + func_name};
    }
  }
  if(sym == nullptr)
  {
    if((sym = try_function_scope(std::string{})) != nullptr)
      symbol_id = irep_idt{"python::" + func_name};
  }
  // If no code symbol was found, fall back to the bare name
  // for the existing variable / class-instance / no-body paths.
  if(sym == nullptr)
  {
    symbol_id = irep_idt{"python::" + func_name};
    sym = symbol_table.lookup(symbol_id);
  }
  // The former ad-hoc math-function block has been retired.
  // math.py declares each function with @c_intrinsic('name',
  // fold='op', domain='kind', range='kind').
  // The decorator path a few hundred lines below handles the
  // complete semantics: parse-time fold for constants, guarded
  // ValueError for domain violations, and range-constrained
  // nondet return for symbolic arguments.

  // Check function aliases (lambda assignments: double = lambda x: x*2,
  // decorator application: @dec def f → f = dec(f), etc.)
  // PLR §8.7: decorators rebind the function name to the wrapper.
  // We check aliases FIRST so that a decorated function dispatches
  // through its wrapper even though the original symbol exists.
  {
    auto alias_it = function_aliases.find(qualify_name(func_name));
    if(alias_it != function_aliases.end())
    {
      const symbolt *alias_sym = symbol_table.lookup(alias_it->second);
      if(alias_sym != nullptr && alias_sym->type.id() == ID_code)
        sym = alias_sym;
    }
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
        // PLR §8.7: f(*xs) where xs is a list-typed expression
        // (e.g. a parameter, not a literal). The target
        // function's arity tells us how many elements to read
        // from the list. Generate xs.data[0], xs.data[1], ...,
        // xs.data[target_arity-1] for the remaining slots.
        // If the list is shorter at run time, the trailing
        // reads return zero (the list buffer is fixed-size and
        // zero-initialised), which over-approximates a Python
        // TypeError-at-runtime as a precision-loss but is sound
        // for the common case where the caller correctly forwards.
        if(is_python_list_type(inner.type()))
        {
          // Number of remaining positional slots in the target
          // (params already provided so far excluded).
          std::size_t already = arguments.size();
          // Target arity: regular params (non-vararg). If the
          // target's last param is itself list-typed (*args), we
          // expand up to that param and let downstream packing
          // re-pack the rest.
          std::size_t remaining = 0;
          if(params.size() > already)
            remaining = params.size() - already;
          // Cap at PYTHON_MAX_LIST_LENGTH so we don't read past
          // the buffer.
          if(remaining > PYTHON_MAX_LIST_LENGTH)
            remaining = PYTHON_MAX_LIST_LENGTH;
          if(remaining > 0)
          {
            const auto &list_st = to_struct_type(inner.type());
            const auto &data_type =
              to_array_type(list_st.components()[1].type());
            exprt data_member = member_exprt{inner, "data", data_type};
            for(std::size_t i = 0; i < remaining; i++)
            {
              exprt idx = from_integer(
                static_cast<long long>(i), signedbv_typet{64});
              arguments.push_back(
                index_exprt{data_member, idx, data_type.element_type()});
            }
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

  // PLR §8.7: pack *args BEFORE keyword handling. The keyword loop
  // resizes arguments to params.size() and writes kwarg values to
  // their named slots, including kwonly slots after *args. If we
  // packed AFTER, the kwonly slot's value would be swept into the
  // *args list (wrong) or the positionals at *args slot would be
  // overwritten by later kwarg writes. Packing first leaves a
  // single packed-list entry at va_idx and nil placeholders at
  // kwonly slots, which the keyword loop then fills cleanly.
  {
    auto va_it_pre = function_vararg_index.find(sym->name);
    if(
      va_it_pre != function_vararg_index.end() &&
      va_it_pre->second < params.size())
    {
      std::size_t va_idx = va_it_pre->second;
      const auto &va_param_type = params[va_idx].type();
      if(is_python_list_type(va_param_type))
      {
        const auto &list_st = to_struct_type(va_param_type);
        const auto &data_type =
          to_array_type(list_st.components()[1].type());
        exprt::operandst elems;
        for(std::size_t i = va_idx; i < arguments.size(); i++)
        {
          exprt arg = arguments[i];
          if(arg.is_nil())
            continue;
          if(arg.type() != data_type.element_type())
            arg = safe_typecast(arg, data_type.element_type());
          elems.push_back(arg);
        }
        std::size_t n_packed = elems.size();
        while(elems.size() < PYTHON_MAX_LIST_LENGTH)
          elems.push_back(safe_zero(data_type.element_type()));
        exprt length =
          from_integer(static_cast<long long>(n_packed), signedbv_typet{64});
        exprt packed = struct_exprt{
          {length, array_exprt{std::move(elems), data_type}}, va_param_type};
        arguments.resize(va_idx);
        arguments.push_back(std::move(packed));
        // Pad to params.size() so kwarg loop can index kwonly slots.
        while(arguments.size() < params.size())
          arguments.push_back(nil_exprt{});
      }
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

  // PLR §8.7: *args packing — the primary packing happens
  // BEFORE keyword handling (above). Only the legacy "trailing
  // list param without function_vararg_index" fallback runs
  // here; functions with a recorded vararg index already had
  // their *args packed.
  {
    auto va_it = function_vararg_index.find(sym->name);
    if(va_it != function_vararg_index.end() && va_it->second < params.size())
    {
      // Already packed by the early pass — nothing to do.
    }
    else if(arguments.size() > params.size() && !params.empty())
    {
      // Fallback for the legacy "trailing list param" case where
      // function_vararg_index wasn't populated (e.g. dynamically
      // synthesised functions). Same as before: if the LAST param
      // is list-typed, pack trailing positionals into it.
      const auto &last_param_type = params.back().type();
      if(is_python_list_type(last_param_type))
      {
        std::size_t n_regular = params.size() - 1;
        const auto &list_st = to_struct_type(last_param_type);
        const auto &data_type = to_array_type(list_st.components()[1].type());
        exprt::operandst elems;
        for(std::size_t i = n_regular; i < arguments.size(); i++)
        {
          exprt arg = arguments[i];
          if(arg.type() != data_type.element_type())
            arg = safe_typecast(arg, data_type.element_type());
          elems.push_back(arg);
        }
        std::size_t n_packed = elems.size();
        while(elems.size() < PYTHON_MAX_LIST_LENGTH)
          elems.push_back(safe_zero(data_type.element_type()));
        exprt length =
          from_integer(static_cast<long long>(n_packed), signedbv_typet{64});
        exprt packed = struct_exprt{
          {length, array_exprt{std::move(elems), data_type}}, last_param_type};
        arguments.resize(n_regular);
        arguments.push_back(std::move(packed));
      }
    }
  }
  // Empty-args fallback: if we have a vararg param and no
  // positionals reached its slot, pass an empty list.
  {
    auto va_it = function_vararg_index.find(sym->name);
    if(va_it != function_vararg_index.end() && va_it->second < params.size())
    {
      std::size_t va_idx = va_it->second;
      if(va_idx >= arguments.size() || arguments[va_idx].is_nil())
      {
        const auto &va_param_type = params[va_idx].type();
        if(is_python_list_type(va_param_type))
        {
          const auto &list_st = to_struct_type(va_param_type);
          const auto &data_type = to_array_type(list_st.components()[1].type());
          exprt::operandst elems;
          while(elems.size() < PYTHON_MAX_LIST_LENGTH)
            elems.push_back(safe_zero(data_type.element_type()));
          exprt length = from_integer(0LL, signedbv_typet{64});
          exprt packed = struct_exprt{
            {length, array_exprt{std::move(elems), data_type}}, va_param_type};
          while(arguments.size() < va_idx)
            arguments.push_back(nil_exprt{});
          if(arguments.size() == va_idx)
            arguments.push_back(std::move(packed));
          else
            arguments[va_idx] = std::move(packed);
          while(arguments.size() < params.size())
            arguments.push_back(nil_exprt{});
        }
      }
    }
  }

  // Typecast arguments to match parameter types
  for(std::size_t i = 0; i < arguments.size() && i < params.size(); i++)
  {
    if(!arguments[i].is_nil() && arguments[i].type() != params[i].type())
    {
      // Class / list / dict reference: struct arg → pointer param.
      // address_of needs an lvalue, so symbol_exprt arguments are
      // taken-address-of directly. Literal struct_exprt or other
      // rvalues are first materialized into a fresh local
      // (pending_checks-staged so the materialization fires before
      // the call) so address_of has something to point at.
      //
      // We accept any pointer-to-struct param type. Element-type
      // mismatches between the argument's struct (e.g. list[str])
      // and the parameter's pointee struct (e.g. list[int]) are
      // bridged with a final typecast on the address_of, matching
      // the existing safe_typecast struct->pointer path that
      // CBMC's symex chases through dereferences.
      if(
        params[i].type().id() == ID_pointer &&
        arguments[i].type().id() == ID_struct &&
        to_pointer_type(params[i].type()).base_type().id() == ID_struct)
      {
        exprt addressable = arguments[i];
        if(addressable.id() != ID_symbol)
        {
          static unsigned mat_ctr = 0;
          std::string tn = "__byref_arg_" + std::to_string(mat_ctr++);
          std::string tq = qualify_name(tn);
          irep_idt ti{tq};
          if(symbol_table.lookup(ti) == nullptr)
          {
            symbolt ts{ti, addressable.type(), "python"};
            ts.base_name = tn;
            ts.is_lvalue = true;
            ts.is_state_var = true;
            ts.is_static_lifetime = current_function.empty();
            symbol_table.add(ts);
          }
          symbol_exprt mat_sym = symbol_table.lookup_ref(ti).symbol_expr();
          pending_checks.push_back(code_frontend_assignt{mat_sym, addressable});
          addressable = mat_sym;
        }
        exprt addr = address_of_exprt{addressable};
        if(addr.type() != params[i].type())
          addr = typecast_exprt{addr, params[i].type()};
        arguments[i] = std::move(addr);
        // PLR §3.1: passing a mutable container by reference means
        // the callee may mutate it. Invalidate the literal cache so
        // subsequent reads at the call site don't constant-fold
        // against the pre-call snapshot.
        if(addressable.id() == ID_symbol)
        {
          irep_idt sid = to_symbol_expr(addressable).get_identifier();
          list_literals.erase(sid);
          dict_literals.erase(sid);
          tuple_literals.erase(sid);
          string_constants.erase(sid);
          float_constants.erase(sid);
        }
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
        // Any-erasure detection: when the parameter is Any-typed
        // (python_value_type) and the caller's argument has a
        // known concrete class type, look up the function's
        // collected `param.X` references in
        // `function_param_attr_uses`. For each X that is NOT a
        // method on the argument's class, emit an
        // attribute-error property anchored at the call site.
        if(
          python_check_any_arg_attrs &&
          is_python_value_type(params[i].type()) &&
          (arguments[i].type().id() == ID_struct ||
           arguments[i].type().id() == ID_struct_tag))
        {
          // Resolve the class tag.
          std::string arg_class_tag;
          if(arguments[i].type().id() == ID_struct_tag)
          {
            const auto &tag = to_struct_tag_type(arguments[i].type());
            const symbolt *sym = symbol_table.lookup(tag.get_identifier());
            if(sym != nullptr && sym->type.id() == ID_struct)
              arg_class_tag = id2string(to_struct_type(sym->type).get_tag());
          }
          else
            arg_class_tag =
              id2string(to_struct_type(arguments[i].type()).get_tag());
          // Look up the function's parameter name. params[i] has
          // identifier "python::<func>::<param_name>".
          irep_idt param_id = params[i].get_identifier();
          auto it = function_param_attr_uses.find(param_id);
          if(it != function_param_attr_uses.end() && !arg_class_tag.empty())
          {
            // Strip "python_class_" prefix if present.
            std::string class_name = arg_class_tag;
            if(class_name.rfind("python_class_", 0) == 0)
              class_name = class_name.substr(13);
            auto cdm = class_declared_methods.find(class_name);
            // Boto3 base methods (inherited helpers) — do not
            // flag these even if absent from the class's own
            // declared methods.
            static const std::set<std::string> boto3_base_methods{
              "get_paginator",
              "can_paginate",
              "get_waiter",
              "close",
              "exceptions",
              "meta",
              "generate_presigned_url",
              "generate_presigned_post"};
            for(const auto &attr_name : it->second)
            {
              if(boto3_base_methods.count(attr_name) > 0)
                continue;
              bool found = false;
              if(
                cdm != class_declared_methods.end() &&
                cdm->second.count(attr_name) > 0)
                found = true;
              // Also accept class-level fields declared in
              // the class's struct (e.g. `year: int` on
              // datetime). The Any-typed-parameter analysis
              // only collected attribute names without
              // distinguishing field-vs-method use, so a
              // bare `obj.year` would erroneously land in
              // the "missing method" path.
              if(!found)
              {
                auto ct = class_types.find(class_name);
                if(ct != class_types.end())
                {
                  for(const auto &comp : ct->second.components())
                  {
                    if(id2string(comp.get_name()) == attr_name)
                    {
                      found = true;
                      break;
                    }
                  }
                }
              }
              if(!found)
              {
                add_check(
                  false_exprt{},
                  "attribute-error",
                  "argument " + std::to_string(i) + " of class '" + class_name +
                    "' missing method '" + attr_name +
                    "' referenced via Any-typed parameter '" +
                    id2string(params[i].get_base_name()) + "' in '" +
                    func_name + "'",
                  get_location(expr));
              }
            }
          }
        }
        // PLR §3.1: when binding a class-instance Name argument
        // to a python_value parameter, pass the address of the
        // caller's storage rather than wrapping a fresh copy.
        // Otherwise mutations the callee performs through the
        // parameter (`p.attr = X`) hit the copy and are
        // invisible to the caller. Restricted to symbol-typed
        // argument expressions (i.e. plain Names) so that
        // expression results from method returns continue to
        // materialise their own backing storage.
        if(
          arguments[i].id() == ID_symbol &&
          is_python_value_type(params[i].type()) &&
          (arguments[i].type().id() == ID_struct ||
           arguments[i].type().id() == ID_struct_tag))
        {
          std::string atag;
          if(arguments[i].type().id() == ID_struct)
            atag = id2string(to_struct_type(arguments[i].type()).get_tag());
          else
            atag = id2string(
              to_struct_tag_type(arguments[i].type()).get_identifier());
          if(atag.compare(0, 13, "python_class_") == 0)
          {
            // Resolve through any class_tag we recorded for this
            // class so the CLASS-tagged python_value carries a
            // valid runtime tag. Set the tag on the storage
            // first so isinstance dispatches correctly.
            std::string cname = atag.substr(13);
            auto ti = class_tag_ids.find(cname);
            if(ti != class_tag_ids.end())
            {
              pending_checks.push_back(code_frontend_assignt{
                member_exprt{arguments[i], "__class_tag", signedbv_typet{32}},
                from_integer(ti->second, signedbv_typet{32})});
            }
            arguments[i] = make_python_value(
              python_type_tagt::CLASS, address_of_exprt{arguments[i]});
            continue;
          }
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
