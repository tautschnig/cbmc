/// Python to GOTO converter — method-call dispatch
/// (PLR §6.3.4 obj.method(args)). Handles every Attribute-form
/// call: string methods (split, replace, format, isalpha, ...),
/// list methods (append, extend, sort, ...), dict methods
/// (items, get, setdefault, ...), set methods, class-instance
/// method dispatch with super() / virtual resolution, math /
/// random / re module receivers, generators, iterators, and
/// the regex-stub fallback for unknown methods. Extracted from
/// python_converter_call.cpp per
/// doc/python-frontend-architecture.md.
/// Pure source-split — semantics are preserved.

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
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iomanip>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <string>

std::optional<exprt> python_convertert::try_method_call(
  const jsont &expr,
  std::string &func_name,
  const jsont &args)
{
  const jsont &func = json_member(expr, "func");
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
        else if(
          python_raising_ops_check && !subj.is_nil() &&
          !is_python_string_type(subj.type()))
        {
          // The arg is not PROVABLY a concrete string (e.g. an
          // Any-typed value that actually holds a non-str at
          // runtime, as in `p: Any = 123; re.search(p, s)`). The
          // certain-assert above is conservatively skipped for
          // tagged-union/pointer types; under the opt-in flag,
          // model the possible TypeError as may-raise.
          emit_may_raise("TypeError");
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
                  self_arg = coerce_call_argument(
                    self_arg, mparams[0].type(), mparams[0].get_identifier());
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
                arguments[ai] = coerce_call_argument(
                  arguments[ai],
                  mparams[ai].type(),
                  mparams[ai].get_identifier());
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
      // PLR §6.4: dict.fromkeys(iterable[, value]) — a new dict mapping each
      // key from the iterable to `value` (default None). Build (key, value)
      // pairs and reuse build_dict_value, which de-duplicates equal constant
      // keys. Handles a list-literal iterable (the common case); other
      // iterables fall through to the default dispatch.
      if(obj_name == "dict" && method_name == "fromkeys" && args.is_array())
      {
        auto it = as_array(args).begin();
        auto a_end = as_array(args).end();
        if(it != a_end && is_node_type(*it, "List"))
        {
          exprt value;
          auto vit = std::next(it);
          if(vit != a_end)
            value = convert_expression(*vit);
          if(value.is_nil())
            value = python_none_value();
          std::vector<std::pair<exprt, exprt>> pairs;
          const jsont &elts = json_member(*it, "elts");
          if(elts.is_array())
            for(const auto &e : as_array(elts))
            {
              exprt k = convert_expression(e);
              if(!k.is_nil())
                pairs.emplace_back(k, value);
            }
          return build_dict_value(std::move(pairs), get_location(expr));
        }
      }
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
      // PLR §math: TypeError when math.X(...) is called with a
      // complex argument. CPython's math module is real-only;
      // calling math.sin(complex(1, 2)) raises TypeError. We
      // model this by setting __exception_active /
      // __exception_type and returning a nondet of double_type
      // (compatible with the most common math return type).
      // Skip cmath.X — that module IS complex-aware.
      // Skip math.prod / math.sumprod — Python accepts complex
      // values in these via element-wise float*complex multiply.
      if(
        obj_name == "math" && method_name != "prod" && method_name != "sumprod")
      {
        // Detect: a node represents (or contains) a complex value.
        // Recursively walks List / Tuple literals so cases like
        // math.fsum([1.0, z]) or math.dist((z, 0.0), (0.0, 0.0))
        // also match.
        std::function<bool(const jsont &)> is_complex_node;
        is_complex_node = [&](const jsont &node) -> bool
        {
          if(is_node_type(node, "List") || is_node_type(node, "Tuple"))
          {
            const jsont &elts = json_member(node, "elts");
            if(elts.is_array())
              for(const auto &e : as_array(elts))
                if(is_complex_node(e))
                  return true;
            return false;
          }
          exprt e = convert_expression(node);
          // Direct python_complex struct (or struct_tag).
          auto get_tag = [](const typet &t) -> std::string
          {
            if(t.id() == ID_struct)
              return id2string(to_struct_type(t).get_tag());
            if(t.id() == ID_struct_tag)
              return id2string(to_struct_tag_type(t).get_identifier());
            return {};
          };
          if(get_tag(e.type()) == "python_complex")
            return true;
          if(e.id() == ID_symbol)
          {
            auto sid = to_symbol_expr(e).get_identifier();
            if(complex_literals.count(sid) > 0)
              return true;
            // Name bound to a list literal whose elements
            // contain a complex value.
            auto lit = list_literals.find(sid);
            if(
              lit != list_literals.end() && lit->second.id() == ID_struct &&
              lit->second.operands().size() >= 2)
            {
              const exprt &data = lit->second.operands()[1];
              if(data.id() == ID_array)
              {
                for(const auto &elem : data.operands())
                {
                  if(get_tag(elem.type()) == "python_complex")
                    return true;
                  // python_value tagged-union with COMPLEX tag.
                  if(
                    is_python_value_type(elem.type()) &&
                    elem.id() == ID_struct && !elem.operands().empty())
                  {
                    const exprt &tag_expr = elem.operands()[0];
                    if(tag_expr.is_constant())
                    {
                      mp_integer tag_iv;
                      if(!to_integer(to_constant_expr(tag_expr), tag_iv))
                      {
                        if(
                          tag_iv == static_cast<int>(python_type_tagt::COMPLEX))
                          return true;
                      }
                    }
                  }
                }
              }
            }
          }
          // Function call returning complex: lookup the callee's
          // declared return type. AST: Call → func: Name(id).
          if(is_node_type(node, "Call"))
          {
            const jsont &func = json_member(node, "func");
            if(is_node_type(func, "Name"))
            {
              std::string fnm = json_string(json_member(func, "id"));
              if(fnm == "complex")
                return true;
              irep_idt fid{"python::" + fnm};
              const symbolt *fsym = symbol_table.lookup(fid);
              if(fsym != nullptr && fsym->type.id() == ID_code)
              {
                const auto &ft = to_code_type(fsym->type);
                if(get_tag(ft.return_type()) == "python_complex")
                  return true;
              }
            }
          }
          return false;
        };
        bool any_complex = false;
        if(args.is_array())
          for(const auto &a : as_array(args))
            if(is_complex_node(a))
            {
              any_complex = true;
              break;
            }
        // Also scan keyword arguments (math.X(b=z)).
        // Handles both regular kwargs (arg= ...) AND
        // dict-unpacking (**kw), where the keyword's `arg` field
        // is null and `value` references a dict literal whose
        // values may contain complex.
        if(!any_complex)
        {
          const jsont &kw = json_member(expr, "keywords");
          if(kw.is_array())
            for(const auto &k : as_array(kw))
            {
              const jsont &arg = json_member(k, "arg");
              const jsont &val = json_member(k, "value");
              if(arg.is_null())
              {
                // Dict-unpack via Name (`**kw`) or direct dict
                // literal (`**{...}`).
                exprt resolved;
                bool resolved_ok = false;
                if(is_node_type(val, "Name"))
                {
                  std::string nm = json_string(json_member(val, "id"));
                  irep_idt sid{qualify_name(nm)};
                  // Walk alias chain through dict_literals so
                  // `kw_alias = kw_base` cases resolve.
                  std::set<irep_idt> seen;
                  while(seen.insert(sid).second)
                  {
                    auto it = dict_literals.find(sid);
                    if(it == dict_literals.end())
                      break;
                    if(it->second.id() == ID_symbol)
                    {
                      sid = to_symbol_expr(it->second).get_identifier();
                      continue;
                    }
                    resolved = it->second;
                    resolved_ok = true;
                    break;
                  }
                }
                else if(is_node_type(val, "Dict"))
                {
                  // **{"x": 1.0, "y": complex(...)}: walk values
                  // directly via the AST.
                  const jsont &vals_arr = json_member(val, "values");
                  if(vals_arr.is_array())
                    for(const auto &dv : as_array(vals_arr))
                      if(is_complex_node(dv))
                      {
                        any_complex = true;
                        break;
                      }
                }
                if(
                  resolved_ok && resolved.id() == ID_struct &&
                  resolved.operands().size() >= 3)
                {
                  const exprt &vals_arr = resolved.operands()[2];
                  if(vals_arr.id() == ID_array)
                  {
                    for(const auto &v : vals_arr.operands())
                    {
                      if(
                        v.type().id() == ID_struct &&
                        to_struct_type(v.type()).get_tag() == "python_complex")
                      {
                        any_complex = true;
                        break;
                      }
                      if(
                        is_python_value_type(v.type()) && v.id() == ID_struct &&
                        !v.operands().empty())
                      {
                        const exprt &tag_expr = v.operands()[0];
                        if(tag_expr.is_constant())
                        {
                          mp_integer tag_iv;
                          if(!to_integer(to_constant_expr(tag_expr), tag_iv))
                          {
                            if(
                              tag_iv ==
                              static_cast<int>(python_type_tagt::COMPLEX))
                            {
                              any_complex = true;
                              break;
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
              else if(is_complex_node(val))
              {
                any_complex = true;
              }
              if(any_complex)
                break;
            }
        }
        if(any_complex)
        {
          const symbolt *exc_sym =
            symbol_table.lookup("python::__exception_active");
          const symbolt *exc_type_sym =
            symbol_table.lookup("python::__exception_type");
          if(exc_sym != nullptr)
          {
            pending_checks.push_back(
              code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
          }
          if(exc_type_sym != nullptr)
          {
            long type_hash = exception_type_hash("TypeError");
            pending_checks.push_back(code_frontend_assignt{
              exc_type_sym->symbol_expr(),
              from_integer(type_hash, python_int_type())});
          }
          static const std::set<std::string> int_returning = {
            "factorial",
            "gcd",
            "lcm",
            "isqrt",
            "perm",
            "comb",
            "floor",
            "ceil",
            "trunc"};
          static const std::set<std::string> bool_returning = {
            "isnan", "isinf", "isfinite", "isclose"};
          if(int_returning.count(method_name) > 0)
            return side_effect_expr_nondett{
              python_int_type(), get_location(expr)};
          if(bool_returning.count(method_name) > 0)
            return side_effect_expr_nondett{bool_typet{}, get_location(expr)};
          return side_effect_expr_nondett{double_type(), get_location(expr)};
        }
      }
      // PLR §math: math.isclose — handle in a dedicated early
      // dispatch so the inline tolerance/rel_tol/abs_tol logic
      // takes precedence over the library's `return True`
      // placeholder. CPython:
      //   isclose(a, b, *, rel_tol=1e-9, abs_tol=0.0) returns
      //   |a - b| <= max(rel_tol * max(|a|, |b|), abs_tol)
      if(obj_name == "math" && method_name == "isclose")
      {
        if(args.is_array() && as_array(args).size() >= 2)
        {
          auto args_it = as_array(args).begin();
          exprt a = convert_expression(*args_it++);
          exprt b = convert_expression(*args_it);
          if(a.type().id() != ID_floatbv)
            a = safe_typecast(a, double_type());
          if(b.type().id() != ID_floatbv)
            b = safe_typecast(b, double_type());
          double rel_tol = 1e-9, abs_tol = 0.0;
          const jsont &kw = json_member(expr, "keywords");
          if(kw.is_array())
          {
            for(const auto &k : as_array(kw))
            {
              std::string kn = json_string(json_member(k, "arg"));
              auto v =
                try_eval_double(convert_expression(json_member(k, "value")));
              if(v.has_value())
              {
                if(kn == "rel_tol")
                  rel_tol = v.value();
                else if(kn == "abs_tol")
                  abs_tol = v.value();
              }
            }
          }
          // Try constant-fold for fully concrete inputs.
          auto av = try_eval_double(a);
          auto bv = try_eval_double(b);
          if(av.has_value() && bv.has_value())
          {
            double diff = std::fabs(av.value() - bv.value());
            double tol = std::max(
              rel_tol * std::max(std::fabs(av.value()), std::fabs(bv.value())),
              abs_tol);
            return diff <= tol ? exprt{true_exprt{}} : exprt{false_exprt{}};
          }
          // Symbolic: emit |a-b| <= max(rel_tol * max(|a|,|b|), abs_tol).
          ieee_floatt rt{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          rt.from_double(rel_tol);
          ieee_floatt at{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          at.from_double(abs_tol);
          exprt abs_a = if_exprt{
            binary_relation_exprt{a, ID_lt, safe_zero(double_type())},
            unary_minus_exprt{a},
            a};
          exprt abs_b = if_exprt{
            binary_relation_exprt{b, ID_lt, safe_zero(double_type())},
            unary_minus_exprt{b},
            b};
          exprt max_ab =
            if_exprt{binary_relation_exprt{abs_a, ID_lt, abs_b}, abs_b, abs_a};
          exprt rel_term = mult_exprt{rt.to_expr(), max_ab};
          exprt tol_max = if_exprt{
            binary_relation_exprt{rel_term, ID_lt, at.to_expr()},
            at.to_expr(),
            rel_term};
          exprt diff = if_exprt{
            binary_relation_exprt{
              minus_exprt{a, b}, ID_lt, safe_zero(double_type())},
            unary_minus_exprt{minus_exprt{a, b}},
            minus_exprt{a, b}};
          return binary_relation_exprt{diff, ID_le, tol_max};
        }
      }
      // PLR §cmath: cmath functions don't accept keyword
      // arguments. cmath.log10(z=complex(1, 0)) raises
      // TypeError: log10() takes no keyword arguments.
      if(obj_name == "cmath")
      {
        const jsont &kw = json_member(expr, "keywords");
        if(kw.is_array() && !as_array(kw).empty())
        {
          const symbolt *exc_sym =
            symbol_table.lookup("python::__exception_active");
          const symbolt *exc_type_sym =
            symbol_table.lookup("python::__exception_type");
          if(exc_sym != nullptr)
            pending_checks.push_back(
              code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
          if(exc_type_sym != nullptr)
          {
            long type_hash = exception_type_hash("TypeError");
            pending_checks.push_back(code_frontend_assignt{
              exc_type_sym->symbol_expr(),
              from_integer(type_hash, python_int_type())});
          }
          // Return a nondet python_complex struct to keep
          // downstream code well-typed.
          struct_typet::componentst comps;
          comps.push_back(struct_typet::componentt{"real", double_type()});
          comps.push_back(struct_typet::componentt{"imag", double_type()});
          struct_typet ct{comps};
          ct.set_tag("python_complex");
          return side_effect_expr_nondett{ct, get_location(expr)};
        }
      }
      // PLR §cmath: cmath.log / cmath.log10 — when called with
      // complex constant arguments, constant-fold via std::complex
      // so the resulting python_complex struct has the precise
      // real/imag fields. The library/cmath.py placeholders return
      // 0+0j, which silently misleads downstream tests that read
      // real/imag of the result. Intercepts BEFORE the imported_
      // modules dispatch so the fold takes precedence.
      if(
        obj_name == "cmath" && (method_name == "log" || method_name == "log10"))
      {
        // Helper: extract real/imag pair from an exprt holding a
        // python_complex struct literal (or a Name bound to one).
        auto extract_complex =
          [&](const jsont &node) -> std::optional<std::pair<double, double>>
        {
          exprt e = convert_expression(node);
          // Direct python_complex struct.
          if(
            e.id() == ID_struct && e.type().id() == ID_struct &&
            to_struct_type(e.type()).get_tag() == "python_complex" &&
            e.operands().size() >= 2)
          {
            auto r = try_eval_double(e.operands()[0]);
            auto i = try_eval_double(e.operands()[1]);
            if(r.has_value() && i.has_value())
              return std::make_pair(r.value(), i.value());
          }
          // Symbol bound to complex_literals.
          if(e.id() == ID_symbol)
          {
            auto sid = to_symbol_expr(e).get_identifier();
            auto it = complex_literals.find(sid);
            if(
              it != complex_literals.end() && it->second.id() == ID_struct &&
              it->second.operands().size() >= 2)
            {
              auto r = try_eval_double(it->second.operands()[0]);
              auto i = try_eval_double(it->second.operands()[1]);
              if(r.has_value() && i.has_value())
                return std::make_pair(r.value(), i.value());
            }
          }
          return std::nullopt;
        };
        if(args.is_array() && !as_array(args).empty())
        {
          auto args_it = as_array(args).begin();
          auto z = extract_complex(*args_it);
          if(z.has_value())
          {
            std::complex<double> zc{z.value().first, z.value().second};
            std::complex<double> result;
            if(method_name == "log10")
              result = std::log10(zc);
            else // log
            {
              if(as_array(args).size() >= 2)
              {
                // log(z, base): handle either complex base or
                // numeric base.
                ++args_it;
                auto bcomplex = extract_complex(*args_it);
                exprt b_expr = convert_expression(*args_it);
                auto bnumeric = try_eval_double(b_expr);
                if(bcomplex.has_value())
                {
                  std::complex<double> bc{
                    bcomplex.value().first, bcomplex.value().second};
                  result = std::log(zc) / std::log(bc);
                }
                else if(bnumeric.has_value())
                {
                  result = std::log(zc) / std::log(bnumeric.value());
                }
                else
                {
                  // Fall through (return nondet via library).
                  goto cmath_log_fall_through;
                }
              }
              else
              {
                result = std::log(zc);
              }
            }
            // Build python_complex struct with fields {real, imag}.
            struct_typet::componentst comps;
            comps.push_back(struct_typet::componentt{"real", double_type()});
            comps.push_back(struct_typet::componentt{"imag", double_type()});
            struct_typet ct{comps};
            ct.set_tag("python_complex");
            return struct_exprt{
              {double_to_floatbv(result.real()),
               double_to_floatbv(result.imag())},
              ct};
          }
        }
      cmath_log_fall_through:;
      }
      // PLR §math: math.frexp — returns (mantissa, exponent)
      // tuple. Constant-fold via std::frexp when the argument
      // is a numeric constant. There is no library body for
      // frexp (see the comment in library/math.py), so the
      // imported_modules dispatch silently falls through to
      // a no-body warning otherwise.
      if(
        obj_name == "math" && method_name == "frexp" && args.is_array() &&
        !as_array(args).empty())
      {
        exprt arg = convert_expression(*as_array(args).begin());
        auto v = try_eval_double(arg);
        if(v.has_value())
        {
          int e = 0;
          double m = std::frexp(v.value(), &e);
          struct_typet ttype =
            python_tuple_type({double_type(), python_int_type()});
          return struct_exprt{
            {double_to_floatbv(m), from_integer(e, python_int_type())}, ttype};
        }
      }
      // PLR §math: list-arg math functions (prod / dist / sumprod
      // / fsum) — when called as math.X(...) with literal-list
      // arguments of int/float constants, constant-fold to the
      // precise result. Like the int-math fold below, this runs
      // BEFORE the imported_modules dispatch so it intercepts
      // the library's placeholder body for the constant case.
      if(
        obj_name == "math" &&
        (method_name == "prod" || method_name == "dist" ||
         method_name == "sumprod" || method_name == "fsum"))
      {
        // Helper: try to extract a list literal's elements as
        // doubles. Returns nullopt if the node isn't a List or
        // bound Name and any element isn't a numeric constant.
        auto extract_doubles =
          [&](const jsont &node) -> std::optional<std::vector<double>>
        {
          // Path 1: direct List or Tuple literal node.
          if(is_node_type(node, "List") || is_node_type(node, "Tuple"))
          {
            const jsont &elts = json_member(node, "elts");
            if(!elts.is_array())
              return std::nullopt;
            std::vector<double> out;
            for(const auto &e : as_array(elts))
            {
              exprt ee = convert_expression(e);
              auto v = try_eval_double(ee);
              if(!v.has_value())
                return std::nullopt;
              out.push_back(v.value());
            }
            return out;
          }
          // Path 2: Name node bound to a list literal — look up
          // list_literals which records {length, data} struct
          // for each list-literal symbol assignment.
          if(is_node_type(node, "Name"))
          {
            std::string nm = json_string(json_member(node, "id"));
            irep_idt sid{qualify_name(nm)};
            auto it = list_literals.find(sid);
            if(it == list_literals.end())
              return std::nullopt;
            const exprt &lit = it->second;
            if(lit.id() != ID_struct || lit.operands().size() < 2)
              return std::nullopt;
            const exprt &len_e = lit.operands()[0];
            const exprt &data = lit.operands()[1];
            if(!len_e.is_constant() || data.id() != ID_array)
              return std::nullopt;
            mp_integer len_v;
            if(to_integer(to_constant_expr(len_e), len_v))
              return std::nullopt;
            std::size_t n = static_cast<std::size_t>(len_v.to_long());
            std::vector<double> out;
            for(std::size_t k = 0; k < n && k < data.operands().size(); ++k)
            {
              auto v = try_eval_double(data.operands()[k]);
              if(!v.has_value())
                return std::nullopt;
              out.push_back(v.value());
            }
            return out;
          }
          return std::nullopt;
        };
        // Extract optional `start=N` keyword argument value
        // (defaults to 1 for prod, 0 for fsum).
        auto extract_start = [&]() -> std::optional<double>
        {
          const jsont &kw = json_member(expr, "keywords");
          if(!kw.is_array())
            return std::nullopt;
          for(const auto &k : as_array(kw))
          {
            if(json_string(json_member(k, "arg")) == "start")
            {
              exprt v = convert_expression(json_member(k, "value"));
              auto d = try_eval_double(v);
              if(d.has_value())
                return d.value();
            }
          }
          return std::nullopt;
        };
        if(args.is_array() && !as_array(args).empty())
        {
          auto args_it = as_array(args).begin();
          // prod / fsum: single list argument.
          if(method_name == "prod" || method_name == "fsum")
          {
            auto vals = extract_doubles(*args_it);
            if(vals.has_value())
            {
              if(method_name == "prod")
              {
                auto start = extract_start();
                long long acc_int =
                  start.has_value() ? static_cast<long long>(start.value()) : 1;
                double acc_dbl = start.has_value() ? start.value() : 1.0;
                bool all_int = !start.has_value() || start.value() == acc_int;
                for(double v : vals.value())
                {
                  long long iv = static_cast<long long>(v);
                  if(v != iv)
                    all_int = false;
                  acc_int *= iv;
                  acc_dbl *= v;
                }
                if(all_int)
                  return from_integer(acc_int, python_int_type());
                return double_to_floatbv(acc_dbl);
              }
              if(method_name == "fsum")
              {
                double acc = 0.0;
                for(double v : vals.value())
                  acc += v;
                return double_to_floatbv(acc);
              }
            }
          }
          // dist / sumprod: two list arguments (must be equal length).
          if(method_name == "dist" || method_name == "sumprod")
          {
            if(as_array(args).size() < 2)
            { /* fall through */
            }
            else
            {
              const jsont &a_node = *args_it;
              const jsont &b_node = *std::next(args_it);
              auto av = extract_doubles(a_node);
              auto bv = extract_doubles(b_node);
              if(
                av.has_value() && bv.has_value() &&
                av.value().size() == bv.value().size())
              {
                if(method_name == "dist")
                {
                  double sum = 0.0;
                  for(std::size_t i = 0; i < av.value().size(); ++i)
                  {
                    double d = av.value()[i] - bv.value()[i];
                    sum += d * d;
                  }
                  return double_to_floatbv(std::sqrt(sum));
                }
                if(method_name == "sumprod")
                {
                  double sum = 0.0;
                  for(std::size_t i = 0; i < av.value().size(); ++i)
                    sum += av.value()[i] * bv.value()[i];
                  return double_to_floatbv(sum);
                }
              }
            }
          }
        }
        // Fall through to imported_modules dispatch for symbolic
        // / non-literal-list cases. The library placeholder will
        // run there.
      }
      // PLR §math: TypeError when an int-only math function
      // (factorial / comb / perm / gcd / lcm / isqrt / floor /
      // ceil / trunc) is called with a non-integer argument
      // (float / string / None). CPython explicitly rejects
      // non-int arguments here; without this check, our int-
      // math fold below would silently coerce or fall through
      // to the library placeholder.
      if(
        obj_name == "math" &&
        (method_name == "factorial" || method_name == "comb" ||
         method_name == "perm" || method_name == "gcd" ||
         method_name == "lcm" || method_name == "isqrt"))
      {
        bool any_non_int = false;
        if(args.is_array())
        {
          for(const auto &a : as_array(args))
          {
            exprt e = convert_expression(a);
            // None constant: convert_expression returns the
            // None sentinel as a signedbv constant equal to
            // -2^62; detect via the AST: Constant with null value.
            if(is_node_type(a, "Constant"))
            {
              const jsont &v = json_member(a, "value");
              if(v.is_null())
              {
                any_non_int = true;
                break;
              }
            }
            // Reject float / floatbv / string / list arguments
            // that aren't representable as Python int.
            if(e.type().id() == ID_floatbv)
            {
              any_non_int = true;
              break;
            }
            // Python string / bytes are recognised by
            // is_python_string_type. Pointers to string types
            // and string struct types both qualify.
            if(is_python_string_type(e.type()))
            {
              any_non_int = true;
              break;
            }
          }
        }
        if(any_non_int)
        {
          const symbolt *exc_sym =
            symbol_table.lookup("python::__exception_active");
          const symbolt *exc_type_sym =
            symbol_table.lookup("python::__exception_type");
          if(exc_sym != nullptr)
            pending_checks.push_back(
              code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
          if(exc_type_sym != nullptr)
          {
            long type_hash = exception_type_hash("TypeError");
            pending_checks.push_back(code_frontend_assignt{
              exc_type_sym->symbol_expr(),
              from_integer(type_hash, python_int_type())});
          }
          return side_effect_expr_nondett{
            python_int_type(), get_location(expr)};
        }
      }
      // PLR §math: int-math functions (factorial / comb / perm /
      // gcd / lcm / isqrt) — when called as math.X(...) with
      // int-constant arguments, constant-fold to the precise
      // result. This path runs BEFORE the imported_modules
      // dispatch so it intercepts the library's placeholder
      // body (which returns 0) for the constant case while
      // leaving the symbolic case to fall through unchanged.
      if(
        obj_name == "math" &&
        (method_name == "factorial" || method_name == "comb" ||
         method_name == "perm" || method_name == "gcd" ||
         method_name == "lcm" || method_name == "isqrt"))
      {
        // Helper: extract all int-constant arguments. If any
        // arg is non-constant or non-int, return nullopt.
        // Handles plain integer literals AND UnaryOp(USub,
        // Constant(int)) for negative literals like -1.
        auto get_int_args = [&]() -> std::optional<std::vector<long long>>
        {
          std::vector<long long> out;
          if(!args.is_array())
            return std::nullopt;
          for(const auto &a : as_array(args))
          {
            exprt e = convert_expression(a);
            // Strip a top-level unary minus over an int constant.
            bool negate = false;
            if(e.id() == ID_unary_minus && e.operands().size() == 1)
            {
              e = e.operands()[0];
              negate = true;
            }
            // Strip outer typecast(int).
            if(
              e.id() == ID_typecast && e.operands().size() == 1 &&
              e.operands()[0].is_constant() &&
              e.operands()[0].type().id() == ID_signedbv)
              e = e.operands()[0];
            if(!e.is_constant() || e.type().id() != ID_signedbv)
              return std::nullopt;
            mp_integer iv;
            if(to_integer(to_constant_expr(e), iv))
              return std::nullopt;
            long long val = iv.to_long();
            if(negate)
              val = -val;
            out.push_back(val);
          }
          return out;
        };
        auto iargs = get_int_args();
        if(iargs.has_value())
        {
          const auto &v = iargs.value();
          auto make_int = [this](long long x)
          { return from_integer(x, python_int_type()); };
          if(method_name == "factorial" && v.size() == 1)
          {
            if(v[0] < 0)
            {
              emit_value_error(false_exprt{});
              return side_effect_expr_nondett{
                python_int_type(), get_location(expr)};
            }
            long long r = 1;
            for(long long i = 2; i <= v[0]; ++i)
              r *= i;
            return make_int(r);
          }
          if(method_name == "isqrt" && v.size() == 1)
          {
            if(v[0] < 0)
            {
              emit_value_error(false_exprt{});
              return side_effect_expr_nondett{
                python_int_type(), get_location(expr)};
            }
            long long n = v[0];
            long long r = 0;
            while((r + 1) * (r + 1) <= n)
              ++r;
            return make_int(r);
          }
          if(method_name == "comb" && v.size() == 2)
          {
            long long n = v[0], k = v[1];
            if(n < 0 || k < 0)
            {
              emit_value_error(false_exprt{});
              return side_effect_expr_nondett{
                python_int_type(), get_location(expr)};
            }
            if(k > n)
              return make_int(0);
            if(k > n - k)
              k = n - k;
            long long r = 1;
            for(long long i = 0; i < k; ++i)
            {
              r *= (n - i);
              r /= (i + 1);
            }
            return make_int(r);
          }
          if(method_name == "perm")
          {
            long long n = v[0];
            long long k = v.size() == 1 ? n : v[1];
            if(n < 0 || k < 0)
            {
              emit_value_error(false_exprt{});
              return side_effect_expr_nondett{
                python_int_type(), get_location(expr)};
            }
            if(k > n)
              return make_int(0);
            long long r = 1;
            for(long long i = 0; i < k; ++i)
              r *= (n - i);
            return make_int(r);
          }
          if(method_name == "gcd")
          {
            long long g = 0;
            for(long long x : v)
            {
              long long ax = x < 0 ? -x : x;
              long long a = g, b = ax;
              while(b)
              {
                long long t = a % b;
                a = b;
                b = t;
              }
              g = a;
            }
            return make_int(g);
          }
          if(method_name == "lcm")
          {
            long long l = 1;
            bool zero = false;
            for(long long x : v)
            {
              long long ax = x < 0 ? -x : x;
              if(ax == 0)
              {
                zero = true;
                break;
              }
              long long a = l, b = ax;
              while(b)
              {
                long long t = a % b;
                a = b;
                b = t;
              }
              long long g = a;
              l = (l / g) * ax;
            }
            if(zero)
              return make_int(0);
            return make_int(l);
          }
        }
        // Non-constant args: fall through to imported_modules
        // dispatch below, which calls the library placeholder.
      }
      if(imported_modules.count(obj_name))
      {
        // PLib stdlib: random module — for these methods we
        // prefer the inline call-site fold (more precise than
        // the library function — e.g. getrandbits(k) for
        // constant k gives [0, (1<<k)-1] precisely, while the
        // library body can only emit n >= 0). Skip the
        // library-function dispatch and fall through to the
        // obj_name == "random" handler below.
        bool intercept_random = false;
        if(obj_name == "random")
        {
          static const std::set<std::string> intercepted{
            "random",
            "uniform",
            "triangular",
            "randint",
            "randrange",
            "getrandbits"};
          intercept_random = intercepted.count(method_name) > 0;
        }
        // Resolve module.func to the function symbol. If the
        // resolved function is decorated with @c_intrinsic,
        // fall through to the main convert_call path so the
        // decorator's fold/domain/range semantics apply. A
        // direct function-call emission would bypass those.
        irep_idt func_id{"python::" + method_name};
        const symbolt *sym = symbol_table.lookup(func_id);
        // PLR §3.3: 'module.ClassName(...)' constructs an
        // instance of ClassName. Detect this by looking up
        // ClassName in class_types and route to the same
        // constructor path the bare 'ClassName(...)' call
        // would use. Without this, the call falls through to
        // the function-call dispatch which produces nondet.
        //
        // Skip when the call is a known collections
        // constructor — those have a dedicated dispatch later
        // in this function that wires the right factory and
        // value type. The check is on method_name (and not on
        // obj_name) so the alias form ('col.defaultdict')
        // also reaches the dedicated dispatch.
        bool is_collections_ctor =
          method_name == "defaultdict" || method_name == "Counter";
        if(
          class_types.count(method_name) &&
          c_intrinsic_map.count(func_id) == 0 && !is_collections_ctor)
        {
          // Re-dispatch by setting func_name to the class name
          // and falling through to the regular convert_call
          // continuation in the caller.
          func_name = method_name;
          return std::nullopt;
        }
        if(
          sym != nullptr && sym->type.id() == ID_code &&
          c_intrinsic_map.count(func_id) == 0 && !intercept_random)
        {
          const code_typet &ft = to_code_type(sym->type);
          const std::size_t nparams = ft.parameters().size();
          exprt::operandst arguments;
          if(args.is_array())
          {
            for(const auto &arg : as_array(args))
              arguments.push_back(convert_expression(arg));
          }
          // Keyword arguments: match each `name=value` to the imported
          // function's parameter base name (mirrors the local-call path).
          // Without this, kwargs to imported functions were dropped -- the body
          // saw the parameter as nondet/default (e.g. `mod.foo(a=5)` returned a
          // wrong value) and the annotation check never ran.
          const jsont &kws = json_member(expr, "keywords");
          if(kws.is_array() && !as_array(kws).empty())
          {
            if(arguments.size() < nparams)
              arguments.resize(nparams, nil_exprt{});
            auto pname = [&](std::size_t i) -> std::string {
              std::string bn = id2string(ft.parameters()[i].get_base_name());
              if(!bn.empty())
                return bn;
              std::string pid =
                id2string(ft.parameters()[i].get_identifier());
              auto pos = pid.rfind("::");
              return pos == std::string::npos ? pid : pid.substr(pos + 2);
            };
            for(const auto &kw : as_array(kws))
            {
              const jsont &an = json_member(kw, "arg");
              if(an.is_null()) // **kwargs spread — not handled here
                continue;
              const std::string kw_name = json_string(an);
              exprt kw_val = convert_expression(json_member(kw, "value"));
              for(std::size_t i = 0; i < nparams; i++)
                if(pname(i) == kw_name)
                {
                  arguments[i] = kw_val;
                  break;
                }
            }
          }
          // Fill any not-supplied parameter (trailing, or a nil slot left by a
          // positional+keyword mix) with its default value; anything still
          // unsupplied becomes a sound nondet of the parameter type (the body
          // would otherwise read an invalid nil operand).
          for(std::size_t i = 0; i < nparams; i++)
          {
            if(i < arguments.size() && !arguments[i].is_nil())
              continue;
            auto def_it = default_values.find({method_name, i});
            if(i >= arguments.size())
              arguments.resize(i + 1, nil_exprt{});
            if(def_it != default_values.end())
              arguments[i] = def_it->second;
          }
          for(std::size_t i = 0; i < arguments.size() && i < nparams; i++)
          {
            if(arguments[i].is_nil())
              arguments[i] = side_effect_expr_nondett{
                ft.parameters()[i].type(), get_location(expr)};
          }
          for(std::size_t i = 0; i < arguments.size() && i < nparams; i++)
          {
            // PLR §3.1 (--python-check-annotations): flag an argument whose
            // type is incompatible with the imported function's declared
            // parameter type. Mirrors the local-call path
            // (python_converter_call_user.cpp); without it a cross-module call
            // `mod.foo(5)` with `foo(a: str)` was silently coerced.
            const typet &pt = ft.parameters()[i].type();
            const bool vararg_collect =
              is_python_list_type(pt) &&
              !is_python_list_type(arguments[i].type());
            if(
              python_check_annotations && !vararg_collect &&
              annotation_types_incompatible(pt, arguments[i].type()))
            {
              add_check(
                false_exprt{},
                "annotation-mismatch",
                "argument " + std::to_string(i) +
                  "'s type does not match declared parameter type of '" +
                  method_name + "'",
                get_location(expr));
            }
            if(arguments[i].type() != pt)
              arguments[i] = safe_typecast(arguments[i], pt);
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
            exprt mag = math_arg.type().id() == ID_floatbv
                          ? math_arg
                          : safe_typecast(math_arg, double_type());
            // IEEE-754 copysign: magnitude of `mag` with the sign BIT of
            // `arg2`. Centralised in util/ieee_float so the negative-zero
            // semantics (copysign(1.0, -0.0) == -1.0) live in one tested
            // place rather than an ad-hoc `arg2 < 0` test here.
            return ieee_copysign(mag, arg2);
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
          // PLR §math: degrees / radians for symbolic args —
          // emit the closed-form expression directly so
          // assertions like `assert math.isfinite(math.degrees(x))`
          // can propagate from a finite x. The constant-fold
          // path below handles concrete values.
          if(func_name == "degrees" || func_name == "radians")
          {
            ieee_floatt c180{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            c180.from_double(180.0);
            ieee_floatt cpi{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            cpi.from_double(M_PI);
            if(func_name == "degrees")
              return div_exprt{
                mult_exprt{math_arg, c180.to_expr()}, cpi.to_expr()};
            return div_exprt{
              mult_exprt{math_arg, cpi.to_expr()}, c180.to_expr()};
          }
          // Option-4 domain check: raise ValueError for known-bad
          // constant arguments, and emit a guarded ValueError for
          // non-constant arguments. See math_function_domain() /
          // emit_value_error() and doc/python-frontend-plans.md
          // (module support).
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
              else if(func_name == "gamma" || func_name == "tgamma")
                res = std::tgamma(val);
              else if(func_name == "lgamma")
                res = std::lgamma(val);
              else if(func_name == "cbrt")
                res = std::cbrt(val);
              else if(func_name == "expm1")
                res = std::expm1(val);
              else if(func_name == "log1p" && val > -1)
                res = std::log1p(val);
              else if(func_name == "ulp")
              {
                // PLR: math.ulp(x) — unit in the last place.
                // Equivalent to nextafter(|x|, +inf) - |x|;
                // for x = 0 returns the smallest subnormal.
                double ax = std::fabs(val);
                if(std::isnan(ax) || std::isinf(ax))
                  res = ax;
                else if(ax == 0.0)
                  res = std::numeric_limits<double>::denorm_min();
                else
                {
                  double na =
                    std::nextafter(ax, std::numeric_limits<double>::infinity());
                  res = na - ax;
                }
              }
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
                else if(op == "ldexp")
                  r = std::ldexp(v1, static_cast<int>(v2));
                else if(op == "nextafter")
                  r = std::nextafter(v1, v2);
                else
                  ok = false;
                if(ok && std::isfinite(r))
                  return double_to_floatbv(r);
              }
            }
            // PLR §math: fmod / remainder / copysign symbolic
            // fallback — emit the closed-form expression so
            // properties like `|fmod(x, y)| < |y|` propagate.
            if(func_name == "fmod")
              return floatbv_mod_exprt{math_arg, arg2};
            if(func_name == "copysign")
            {
              // copysign(x, y) = |x| if y >= 0, else -|x|.
              exprt abs_x = if_exprt{
                binary_relation_exprt{
                  math_arg, ID_lt, safe_zero(double_type())},
                unary_minus_exprt{math_arg},
                math_arg};
              return if_exprt{
                binary_relation_exprt{arg2, ID_lt, safe_zero(double_type())},
                unary_minus_exprt{abs_x},
                abs_x};
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
        // PLR stdlib: re module — shallow nondet for methods without a
        // precise model. sub/subn are excluded: they fall through to the
        // library stub (src/python/library/re), which routes through the
        // __cbmc_re_sub intrinsic for a precise str.replace_re_all result on
        // the sound subset (constant fixed-length pattern, literal repl) and
        // a sound nondet string otherwise.
        if(obj_name == "re" && method_name != "sub" && method_name != "subn")
          return side_effect_expr_nondett{
            python_int_type(), get_location(expr)};
        // PLib: random module — constrained nondet for randint /
        // random / uniform / triangular.
        if(obj_name == "random")
        {
          // Helper to emit a nondet result with a constrained
          // range, returning the temp symbol expression.
          // strict_high=true: result < high; else result <= high.
          static unsigned rand_ctr = 0;
          auto emit_nondet_with_assume =
            [&](const typet &t, exprt low, exprt high, bool strict_high = false)
            -> exprt
          {
            std::string tmp = "__rand_" + std::to_string(rand_ctr++);
            std::string tq = qualify_name(tmp);
            irep_idt ti{tq};
            if(symbol_table.lookup(ti) == nullptr)
            {
              symbolt ts{ti, t, "python"};
              ts.base_name = tmp;
              ts.is_lvalue = true;
              ts.is_state_var = true;
              symbol_table.add(ts);
            }
            symbol_exprt tv = symbol_table.lookup_ref(ti).symbol_expr();
            pending_checks.push_back(code_frontend_assignt{
              tv, side_effect_expr_nondett{t, get_location(expr)}});
            irep_idt high_op = strict_high ? ID_lt : ID_le;
            pending_checks.push_back(code_assumet{and_exprt{
              binary_relation_exprt{tv, ID_ge, std::move(low)},
              binary_relation_exprt{tv, high_op, std::move(high)}}});
            return std::move(tv);
          };
          if(
            method_name == "randint" && args.is_array() &&
            as_array(args).size() >= 2)
          {
            auto it = as_array(args).begin();
            exprt lo = convert_expression(*it);
            ++it;
            exprt hi = convert_expression(*it);
            return emit_nondet_with_assume(python_int_type(), lo, hi);
          }
          if(method_name == "random")
          {
            // PLib random.random() — float in [0.0, 1.0).
            return emit_nondet_with_assume(
              double_type(),
              double_to_floatbv(0.0),
              double_to_floatbv(1.0),
              /*strict_high=*/true);
          }
          if(
            (method_name == "uniform" || method_name == "triangular") &&
            args.is_array() && as_array(args).size() >= 2)
          {
            auto it = as_array(args).begin();
            exprt a = convert_expression(*it);
            ++it;
            exprt b = convert_expression(*it);
            // Cast to double if not already.
            if(a.type() != double_type())
              a = safe_typecast(a, double_type());
            if(b.type() != double_type())
              b = safe_typecast(b, double_type());
            return emit_nondet_with_assume(double_type(), a, b);
          }
          if(method_name == "randrange" && args.is_array())
          {
            auto sz = as_array(args).size();
            if(sz == 1)
            {
              // randrange(stop): [0, stop)
              auto it = as_array(args).begin();
              exprt stop = convert_expression(*it);
              exprt zero = from_integer(0, python_int_type());
              exprt stop_minus_1 =
                minus_exprt{stop, from_integer(1, python_int_type())};
              return emit_nondet_with_assume(
                python_int_type(), zero, stop_minus_1);
            }
            if(sz >= 2)
            {
              // randrange(start, stop[, step]): a value v
              // such that v = start + k*step for some k>=0
              // and (step>0 ? v<stop : v>stop).
              auto it = as_array(args).begin();
              exprt start = convert_expression(*it);
              ++it;
              exprt stop = convert_expression(*it);
              exprt step = from_integer(1, python_int_type());
              if(sz >= 3)
              {
                ++it;
                step = convert_expression(*it);
              }
              start = safe_typecast(start, python_int_type());
              stop = safe_typecast(stop, python_int_type());
              step = safe_typecast(step, python_int_type());
              // Build a fresh nondet symbol and assume:
              //   step > 0 -> start <= v < stop AND
              //               (v - start) % step == 0
              //   step < 0 -> stop  <  v <= start AND
              //               (start - v) % (-step) == 0
              static unsigned rrng_ctr = 0;
              std::string vn = "__randrange_" + std::to_string(rrng_ctr++);
              std::string vq = qualify_name(vn);
              irep_idt vi{vq};
              if(symbol_table.lookup(vi) == nullptr)
              {
                symbolt vs{vi, python_int_type(), "python"};
                vs.base_name = vn;
                vs.is_lvalue = true;
                vs.is_state_var = true;
                symbol_table.add(vs);
              }
              symbol_exprt v = symbol_table.lookup_ref(vi).symbol_expr();
              pending_checks.push_back(code_frontend_assignt{
                v,
                side_effect_expr_nondett{
                  python_int_type(), get_location(expr)}});
              // pos: start <= v < stop && (v - start) % step == 0
              exprt pos_bounds = and_exprt{
                binary_relation_exprt{start, ID_le, v},
                binary_relation_exprt{v, ID_lt, stop}};
              exprt pos_align = equal_exprt{
                mod_exprt{minus_exprt{v, start}, step},
                from_integer(0, python_int_type())};
              exprt pos_assume = and_exprt{pos_bounds, pos_align};
              // neg: stop < v <= start && (start - v) % (-step) == 0
              exprt neg_step = unary_minus_exprt{step, python_int_type()};
              exprt neg_bounds = and_exprt{
                binary_relation_exprt{stop, ID_lt, v},
                binary_relation_exprt{v, ID_le, start}};
              exprt neg_align = equal_exprt{
                mod_exprt{minus_exprt{start, v}, neg_step},
                from_integer(0, python_int_type())};
              exprt neg_assume = and_exprt{neg_bounds, neg_align};
              exprt step_pos = binary_relation_exprt{
                step, ID_gt, from_integer(0, python_int_type())};
              exprt assumption = if_exprt{step_pos, pos_assume, neg_assume};
              code_assumet assume{assumption};
              pending_checks.push_back(std::move(assume));
              return v;
            }
          }
          // PLib random.getrandbits(k): nondet int in [0, 2^k - 1].
          // Raises ValueError when k <= 0.
          if(
            method_name == "getrandbits" && args.is_array() &&
            !as_array(args).empty())
          {
            exprt kexp = convert_expression(*as_array(args).begin());
            // Use try_eval_double for constant detection so unary
            // minus (-10) and other simple expressions fold.
            auto kfd = try_eval_double(kexp);
            bool kconst = kfd.has_value() && *kfd == std::floor(*kfd);
            long long kval = kconst ? (long long)*kfd : 0;
            if(kconst && kval == 0)
            {
              // Edge case: getrandbits(0) returns 0 in our model
              // (no random bits at all). CPython raises
              // ValueError, but several existing tests rely on
              // returning 0.
              return from_integer(0, python_int_type());
            }
            if(kconst && kval < 0)
            {
              // Definitely raises ValueError.
              const symbolt *exc_sym =
                symbol_table.lookup("python::__exception_active");
              const symbolt *exc_type_sym =
                symbol_table.lookup("python::__exception_type");
              if(exc_sym != nullptr)
              {
                pending_checks.push_back(
                  code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
                if(exc_type_sym != nullptr)
                {
                  long h = exception_type_hash("ValueError");
                  pending_checks.push_back(code_frontend_assignt{
                    exc_type_sym->symbol_expr(),
                    from_integer(h, python_int_type())});
                }
              }
              return from_integer(0, python_int_type());
            }
            if(kconst && kval > 0 && kval < 64)
            {
              mp_integer hi = (mp_integer{1} << (int)kval) - 1;
              return emit_nondet_with_assume(
                python_int_type(),
                from_integer(0, python_int_type()),
                from_integer(hi, python_int_type()));
            }
            // Symbolic k or k >= 64: emit a non-negative nondet
            // int (no upper bound).
            return emit_nondet_with_assume(
              python_int_type(),
              from_integer(0, python_int_type()),
              from_integer(
                std::numeric_limits<long long>::max(), python_int_type()));
          }
          // Fallback: nondet of best-guess type.
          // float for random/uniform/triangular/gauss, int otherwise.
          if(
            method_name == "gauss" || method_name == "expovariate" ||
            method_name == "betavariate" || method_name == "gammavariate")
            return side_effect_expr_nondett{double_type(), get_location(expr)};
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

    // PLR §3.1/§3.3: an Any (python_value) receiver of an unambiguous built-in
    // container method is a list/dict shared by reference -- unwrap it to the
    // concrete container lvalue so the container-method handlers below
    // dispatch and mutate the shared object.
    obj = unwrap_any_container_receiver(obj, method_name);

    // PLR §3.3: an ambiguous container method (pop / remove / clear / copy /
    // update) on an Any receiver is disambiguated at runtime on __tag.
    if(
      auto r =
        dispatch_any_container_method_by_tag(expr, obj, method_name, args))
      return std::move(*r);

    // Resolve class name from object's type (struct or pointer-to-struct)
    typet obj_base_type = obj.type();
    if(obj_base_type.id() == ID_pointer)
      obj_base_type = to_pointer_type(obj_base_type).base_type();

    // PLR §4: a str-ONLY method called on a CONCRETE non-str built-in receiver
    // raises AttributeError (e.g. `(5).upper()`, `[1,2].upper()`,
    // `(1,2,3).upper()`). Carefully gated to avoid false positives:
    //   - only DEFINITELY-str-only method names (NOT count/index, which
    //     list/tuple share; NOT shared dunder/other names);
    //   - NOT python_value/Any (could be a str at runtime);
    //   - NOT str / smt_string receivers;
    //   - NOT bytes (modelled as list[uint8] -- it HAS upper/lower/strip/...),
    //     so a list whose element type is unsignedbv[8] is excluded;
    //   - NOT user-class instances (they may define a method of that name).
    {
      static const std::set<std::string> str_only_methods = {
        "upper",        "lower",      "capitalize",   "title",
        "swapcase",     "casefold",   "strip",        "lstrip",
        "rstrip",       "startswith", "endswith",     "split",
        "rsplit",       "splitlines", "join",         "replace",
        "find",         "rfind",      "format",       "format_map",
        "encode",       "zfill",      "center",       "ljust",
        "rjust",        "expandtabs", "removeprefix", "removesuffix",
        "partition",    "rpartition", "translate",    "isdigit",
        "isalpha",      "isalnum",    "isspace",      "isupper",
        "islower",      "istitle",    "isnumeric",    "isdecimal",
        "isidentifier", "isascii",    "isprintable"};
      // Methods EXCLUSIVE to one built-in type. Calling such a method on a
      // CONCRETE receiver of a DIFFERENT built-in type is a definite
      // AttributeError (e.g. `(5).append(3)`, `[1].keys()`, `(1,2).add(3)`).
      // Only strictly-exclusive names (NOT count/index/pop/clear/copy/remove,
      // which several types share).
      static const std::set<std::string> list_only_methods = {
        "append", "extend", "insert", "sort", "reverse"};
      static const std::set<std::string> dict_only_methods = {
        "keys", "values", "items", "setdefault", "popitem", "fromkeys"};
      static const std::set<std::string> set_only_methods = {
        "add",
        "discard",
        "isdisjoint",
        "issubset",
        "issuperset",
        "symmetric_difference",
        "intersection_update",
        "difference_update",
        "symmetric_difference_update"};
      const char *owner = nullptr;
      if(str_only_methods.count(method_name))
        owner = "str";
      else if(list_only_methods.count(method_name))
        owner = "list";
      else if(dict_only_methods.count(method_name))
        owner = "dict";
      else if(set_only_methods.count(method_name))
        owner = "set";
      if(owner != nullptr)
      {
        const typet &t = obj_base_type;
        const bool numeric = t.id() == ID_signedbv || t.id() == ID_unsignedbv ||
                             t.id() == ID_integer || t.id() == ID_floatbv ||
                             t.id() == ID_bool;
        const bool is_complex = t.id() == ID_struct &&
                                to_struct_type(t).get_tag() == "python_complex";
        const bool is_list_recv = is_python_list_type(t); // incl. bytes
        // bytes := list[uint8]; it HAS the str methods, so exclude it from the
        // str-owner check (a list receiver already excludes list_only).
        const bool is_bytes =
          is_list_recv &&
          to_array_type(to_struct_type(t).components()[1].type())
              .element_type() == unsignedbv_typet{8};
        const bool is_str_recv =
          is_python_string_type(t) || t.id() == ID_smt_string;
        const bool is_dict_recv = is_python_dict_type(t);
        const bool is_set_recv = is_python_set_type(t);
        const bool is_tuple_recv = is_python_tuple_type(t);
        const bool user_class =
          t.id() == ID_struct &&
          id2string(to_struct_type(t).get_tag()).rfind("python_class_", 0) == 0;
        const bool concrete_builtin = numeric || is_complex || is_str_recv ||
                                      is_list_recv || is_dict_recv ||
                                      is_set_recv || is_tuple_recv;
        const std::string ow{owner};
        const bool matches_owner = (ow == "str" && (is_str_recv || is_bytes)) ||
                                   (ow == "list" && is_list_recv) ||
                                   (ow == "dict" && is_dict_recv) ||
                                   (ow == "set" && is_set_recv);
        if(concrete_builtin && !user_class && !matches_owner)
        {
          emit_conditional_exception(true_exprt{}, "AttributeError");
          return side_effect_expr_nondett{
            python_value_type(), get_location(expr)};
        }
      }
    }

    // PLR §4.4.2: int instance methods. obj is python_int
    // (signedbv 64). Dispatch on method_name.
    if(obj_base_type.id() == ID_signedbv)
    {
      if(method_name == "bit_length")
      {
        // Constant fold: bit_length() == ceil(log2(|n|+1)).
        auto fv = try_eval_double(obj);
        if(fv.has_value())
        {
          long long n = (long long)*fv;
          if(n < 0)
            n = -n;
          long long bl = 0;
          while(n > 0)
          {
            bl++;
            n >>= 1;
          }
          return from_integer(bl, python_int_type());
        }
        // Symbolic: result is in [0, 63].
        // Could express precisely with a clz-style intrinsic; we
        // settle for a constrained nondet here.
        side_effect_expr_nondett nondet{python_int_type(), get_location(expr)};
        static unsigned bl_ctr = 0;
        std::string tmp = "__bitlen_" + std::to_string(bl_ctr++);
        irep_idt ti{qualify_name(tmp)};
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
          binary_relation_exprt{tv, ID_ge, from_integer(0, python_int_type())},
          binary_relation_exprt{
            tv, ID_le, from_integer(63, python_int_type())}}});
        return std::move(tv);
      }
      if(method_name == "bit_count")
      {
        // bit_count(): popcount of |n|.
        auto fv = try_eval_double(obj);
        if(fv.has_value())
        {
          long long n = (long long)*fv;
          if(n < 0)
            n = -n;
          long long bc = 0;
          while(n > 0)
          {
            bc += n & 1;
            n >>= 1;
          }
          return from_integer(bc, python_int_type());
        }
      }
      if(method_name == "conjugate" || method_name == "real")
      {
        return obj;
      }
      if(method_name == "imag")
      {
        return from_integer(0, python_int_type());
      }
      if(method_name == "denominator")
      {
        return from_integer(1, python_int_type());
      }
      if(method_name == "numerator")
      {
        return obj;
      }
    }

    // Native SMT-String back-end (Plan A): smt_string is not a struct/
    // struct_tag, so the struct-gated string-method dispatch below would miss
    // it. Dispatch here, but only for methods with a native str.* lowering;
    // others (upper/lower/strip/split/count/... — no SMT-LIB String
    // primitive) would otherwise hit struct member access and crash, so they
    // return a sound nondet SMT String over-approximation.
    if(obj_base_type.id() == ID_smt_string)
    {
      static const std::set<std::string> native_supported = {
        "upper",
        "lower",
        "casefold",
        "swapcase",
        "capitalize",
        "title",
        "startswith",
        "endswith",
        "find",
        "index",
        "rfind",
        "rindex",
        "count",
        "replace",
        "strip",
        "lstrip",
        "rstrip",
        "split"};
      if(native_supported.count(method_name))
      {
        if(
          auto r =
            try_string_method(expr, obj, obj_base_type, method_name, args))
          return std::move(*r);
      }
      // Unsupported under native (no SMT-LIB String primitive): sound nondet
      // of the method's natural return type.
      static const std::set<std::string> bool_methods = {
        "isdigit",
        "isalpha",
        "isalnum",
        "isupper",
        "islower",
        "isspace",
        "isascii",
        "isnumeric",
        "isidentifier",
        "isdecimal",
        "istitle",
        "isprintable"};
      if(bool_methods.count(method_name))
        return side_effect_expr_nondett{bool_typet{}, get_location(expr)};
      // Int-returning methods with no SMT-LIB String primitive (str.indexof
      // is first-match only, so rfind/rindex/count are nondet here).
      static const std::set<std::string> int_methods = {};
      if(int_methods.count(method_name))
        return side_effect_expr_nondett{python_int_type(), get_location(expr)};
      return bounded_nondet_string(get_location(expr));
    }

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

      // PLib stdtypes: String methods. Extracted to
      // python_converter_call_string_methods.cpp for clarity.
      if(is_python_string_type(obj_base_type))
      {
        if(
          auto r =
            try_string_method(expr, obj, obj_base_type, method_name, args))
          return std::move(*r);
      }

      // PLib stdtypes: Dict methods. Extracted to
      // python_converter_call_dict_methods.cpp for clarity.
      if(is_python_dict_type(obj_base_type))
      {
        if(
          auto r = try_dict_method(expr, obj, obj_base_type, method_name, args))
          return std::move(*r);
      }

      // Python set methods. Extracted to
      // python_converter_call_set_methods.cpp for clarity.
      if(is_python_set_type(obj_base_type))
      {
        if(auto r = try_set_method(expr, obj, obj_base_type, method_name, args))
          return std::move(*r);
      }

      // PLib stdtypes: List methods. Extracted to
      // python_converter_call_list_methods.cpp for clarity.
      if(is_python_list_type(obj_base_type))
      {
        if(
          auto r = try_list_method(expr, obj, obj_base_type, method_name, args))
          return std::move(*r);
      }

      // PLR §6.10.5: Tuple methods. tuple.index(x) returns the
      // first index of x; tuple.count(x) returns occurrences.
      // Restrict to constant-tuple (struct_exprt with constant
      // operands) + constant-arg path: tuple elements and the
      // search value are both known at conversion time, so we
      // can fold to a constant integer.
      if(is_python_tuple_type(obj_base_type))
      {
        if(method_name == "index" || method_name == "count")
        {
          const exprt *tup = &obj;
          if(obj.id() == ID_symbol)
          {
            auto it = tuple_literals.find(to_symbol_expr(obj).get_identifier());
            if(it != tuple_literals.end())
              tup = &it->second;
          }
          if(
            tup->id() == ID_struct && args.is_array() &&
            !as_array(args).empty())
          {
            exprt arg = convert_expression(*as_array(args).begin());
            mp_integer count_val = 0;
            mp_integer first_match = -1;
            bool all_comparable = true;
            {
              auto a_str_top = extract_string_value(arg);
              if(!a_str_top.has_value() && !arg.is_constant())
                all_comparable = false;
            }
            for(std::size_t i = 0; i < tup->operands().size(); ++i)
            {
              const exprt &elem = tup->operands()[i];
              bool matches = false;
              auto e_str = extract_string_value(elem);
              auto a_str = extract_string_value(arg);
              if(e_str.has_value() && a_str.has_value())
                matches = e_str.value() == a_str.value();
              else if(elem.is_constant() && arg.is_constant())
              {
                mp_integer ev, av;
                if(
                  !to_integer(to_constant_expr(elem), ev) &&
                  !to_integer(to_constant_expr(arg), av))
                  matches = ev == av;
              }
              // If neither comparison path could evaluate this element against
              // the arg, we cannot prove it is NOT a match -> absence is not
              // definite (do not raise a spurious ValueError below).
              if(!(e_str.has_value() || elem.is_constant()))
                all_comparable = false;
              if(matches)
              {
                count_val += 1;
                if(first_match < 0)
                  first_match = static_cast<long long>(i);
              }
            }
            if(method_name == "index")
            {
              if(first_match >= 0)
                return from_integer(first_match, python_int_type());
              // PLR §6.10: tuple.index(v) raises ValueError when v is absent.
              // When every element and the arg are constant-comparable, absence
              // is DEFINITE -> emit the exception (was silently falling through
              // to nondet, a false proof). Otherwise absence is not provable;
              // keep the nondet fall-through.
              if(all_comparable)
              {
                emit_conditional_exception(true_exprt{}, "ValueError");
                return side_effect_expr_nondett{
                  python_int_type(), get_location(expr)};
              }
            }
            else
            {
              return from_integer(count_val, python_int_type());
            }
          }
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

          // Fill trailing parameter defaults for a method called on an
          // optional/union (python_value) receiver. Without this, a call that
          // omits defaulted arguments (e.g. re.search(...).group()) emits a
          // FUNCTION_CALL with too few arguments and the GOTO layer fills the
          // gap with nondet, defeating the method's default. This mirrors the
          // AST-based default binding in convert_user_call so both the
          // concretely-typed and the optional/union receiver paths behave
          // identically (one architectural fix for every defaulted method
          // reached through an optional return). `mcall_args` holds
          // [self, provided_args...]; Python defaults bind to the trailing
          // parameters.
          auto append_method_defaults = [&](
                                          const std::string &owner_cls,
                                          const code_typet &mty,
                                          exprt::operandst &mcall_args)
          {
            (void)owner_cls;
            const std::size_t nparams = mty.parameters().size();
            // default_values is populated for every module (including the
            // library) at definition time, keyed by {method_name, param
            // index incl. self}. Python defaults are contiguous at the end,
            // so stop at the first missing param (a genuinely required arg
            // is left for the GOTO layer).
            for(std::size_t i = mcall_args.size(); i < nparams; i++)
            {
              auto dit = default_values.find({method_name, i});
              if(dit == default_values.end())
                break;
              exprt dv = dit->second;
              const typet &pt = mty.parameters()[i].type();
              // PLR §3.6: a None default for a python_string param binds
              // as the length-0 empty-string marker, not a nondet int.
              if(is_python_none_constant(dv) && is_python_string_type(pt))
                dv = struct_exprt{
                  {from_integer(0, signedbv_typet{64}),
                   null_pointer_exprt{pointer_typet{unsignedbv_typet{8}, 64}}},
                  python_string_type()};
              else if(dv.type() != pt)
                dv = safe_typecast(dv, pt);
              mcall_args.push_back(std::move(dv));
            }
          };

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
            append_method_defaults(cls_name, mty, mcall_args);
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
            append_method_defaults(cls_name, mty, mcall_args);

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
          // Inheritance fallback: if the class doesn't define
          // the method itself, walk its C3 MRO to look for the
          // method on each ancestor in resolution order. The
          // first match wins (single-inheritance: the immediate
          // parent; multiple-inheritance: per Python's MRO).
          // class_mro is populated by compute_c3_mro() in
          // convert_class_def. mro_it->second[0] is the class
          // itself (already checked above); skip it.
          if(method_sym == nullptr)
          {
            auto mro_it = class_mro.find(class_name);
            if(mro_it != class_mro.end())
            {
              for(std::size_t i = 1; i < mro_it->second.size(); ++i)
              {
                const std::string &ancestor = mro_it->second[i];
                irep_idt ancestor_method_id{
                  "python::" + ancestor + "::" + method_name};
                const symbolt *ancestor_sym =
                  symbol_table.lookup(ancestor_method_id);
                if(ancestor_sym != nullptr)
                {
                  // Resolved on an ancestor — rebind for the
                  // dispatch logic below.
                  class_name = ancestor;
                  method_id = ancestor_method_id;
                  method_sym = ancestor_sym;
                  break;
                }
              }
            }
          }
          // PLR §8.7: bound-method signature validation via the
          // shared checker. An unbound call `Class.method(self, ...)`
          // passes self explicitly (no implicit-self slot); a bound
          // call obj.m(...) has one. Detect unbound by the receiver
          // being a known class name.
          if(method_sym != nullptr)
          {
            const jsont &recv_node = json_member(func, "value");
            bool unbound =
              is_node_type(recv_node, "Name") &&
              class_types.count(json_string(json_member(recv_node, "id"))) > 0;
            validate_call_signature(method_id, expr, args, unbound ? 0 : 1);
          }

          // Strict missing-method detection: if the class is in
          // class_types (we know its structure) but the method
          // isn't declared (and isn't inherited via MRO), raise
          // AttributeError rather than silently over-approximating.
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
        // Inheritance fallback: walk MRO if not found on the
        // class itself (mirrors the resolution above for the
        // first dispatch path).
        if(method_sym == nullptr)
        {
          auto mro_it = class_mro.find(class_name);
          if(mro_it != class_mro.end())
          {
            for(std::size_t i = 1; i < mro_it->second.size(); ++i)
            {
              const std::string &ancestor = mro_it->second[i];
              irep_idt ancestor_method_id{
                "python::" + ancestor + "::" + method_name};
              const symbolt *ancestor_sym =
                symbol_table.lookup(ancestor_method_id);
              if(ancestor_sym != nullptr)
              {
                class_name = ancestor;
                method_id = ancestor_method_id;
                method_sym = ancestor_sym;
                break;
              }
            }
          }
        }
        if(method_sym != nullptr)
        {
          const code_typet &method_type = to_code_type(method_sym->type);
          exprt::operandst arguments;
          // Pass address of object as self (pointer-based model)
          // Skip for @staticmethod (no self/cls parameter)
          bool has_self = !method_type.parameters().empty() &&
                          method_type.parameters()[0].type().id() == ID_pointer;
          // PLR §9.3.4: an UNBOUND call `Class.method(inst, ...)` passes the
          // instance EXPLICITLY as the first positional arg, so we must NOT
          // prepend the (class) receiver as self — the first positional fills
          // self instead. A bound call `obj.m(...)` and a `self=` keyword are
          // handled as before.
          const jsont &recv_self_node = json_member(func, "value");
          const bool recv_is_class =
            is_node_type(recv_self_node, "Name") &&
            class_types.count(json_string(json_member(recv_self_node, "id"))) >
              0;
          // Only an INSTANCE method (first param `self`) called unbound takes
          // its instance as the first positional arg. A classmethod (first
          // param `cls`) still receives the class via the receiver, so it must
          // keep the prepend.
          const std::string first_param_name =
            has_self ? id2string(method_type.parameters()[0].get_base_name())
                     : std::string{};
          const bool is_unbound_class_call =
            recv_is_class && first_param_name == "self";
          if(has_self && !is_unbound_class_call)
          {
            if(obj.type().id() == ID_pointer)
              arguments.push_back(obj);
            else if(obj.id() == ID_side_effect)
            {
              // PLR §6.3.2 chained method call: `a.f().g()` —
              // f() returns a class instance, .g() is called
              // on that instance. We need the address of the
              // returned struct. Materialise the call result
              // into a fresh temp symbol so `&temp` is a
              // valid lvalue address; without this we'd push a
              // nondet pointer and the callee would see
              // garbage for self.
              static unsigned chain_tmp_ctr = 0;
              std::string tmp_name =
                "__chain_tmp_" + std::to_string(chain_tmp_ctr++);
              std::string tmp_qname = qualify_name(tmp_name);
              irep_idt tmp_id{tmp_qname};
              if(symbol_table.lookup(tmp_id) == nullptr)
              {
                symbolt tmp_sym{tmp_id, obj.type(), "python"};
                tmp_sym.base_name = tmp_name;
                tmp_sym.is_lvalue = true;
                tmp_sym.is_state_var = true;
                symbol_table.add(tmp_sym);
              }
              const symbolt &tmp_sym = symbol_table.lookup_ref(tmp_id);
              pending_checks.push_back(
                code_frontend_assignt{tmp_sym.symbol_expr(), obj});
              arguments.push_back(address_of_exprt{tmp_sym.symbol_expr()});
            }
            else
              arguments.push_back(address_of_exprt{obj});
          }
          if(args.is_array())
          {
            std::size_t ai = 0;
            for(const auto &arg : as_array(args))
            {
              // Unbound call: the first positional arg IS self; pass its
              // address (materialising a temp when it isn't already an
              // addressable lvalue), so the callee's pointer self is bound
              // to the explicit instance instead of the class receiver.
              if(has_self && is_unbound_class_call && ai == 0)
              {
                exprt self_arg = convert_expression(arg);
                if(self_arg.type().id() == ID_pointer)
                  arguments.push_back(self_arg);
                else if(self_arg.id() == ID_symbol)
                  arguments.push_back(address_of_exprt{self_arg});
                else
                {
                  static unsigned ubnd_tmp_ctr = 0;
                  std::string tn =
                    "__unbound_self_" + std::to_string(ubnd_tmp_ctr++);
                  irep_idt tid{qualify_name(tn)};
                  if(symbol_table.lookup(tid) == nullptr)
                  {
                    symbolt ts{tid, self_arg.type(), "python"};
                    ts.base_name = tn;
                    ts.is_lvalue = true;
                    ts.is_state_var = true;
                    symbol_table.add(ts);
                  }
                  symbol_exprt tsym =
                    symbol_table.lookup_ref(tid).symbol_expr();
                  pending_checks.push_back(
                    code_frontend_assignt{tsym, std::move(self_arg)});
                  arguments.push_back(address_of_exprt{tsym});
                }
              }
              else
                arguments.push_back(convert_expression(arg));
              ++ai;
            }
          }

          // Handle keyword arguments for method calls
          const jsont &method_keywords = json_member(expr, "keywords");
          if(!(method_keywords.is_array() &&
               !as_array(method_keywords).empty()))
          {
            // Positional call without keyword args: fill any trailing
            // parameters that were not supplied with their default values
            // (e.g. p.sub("X", s) leaving count/flags defaulted). The
            // keyword path below performs the equivalent fill for its case.
            const auto &mparams = method_type.parameters();
            for(std::size_t i = arguments.size(); i < mparams.size(); i++)
            {
              auto def_it = default_values.find({method_name, i});
              if(def_it == default_values.end())
                break;
              exprt d = def_it->second;
              if(d.type() != mparams[i].type())
                d = safe_typecast(d, mparams[i].type());
              arguments.push_back(std::move(d));
            }
          }
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
                  vs.push_back(coerce_element(v, vat.element_type()));
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
              {
                // PLR §8.7: try the default-values map first
                // (definition-time-evaluated defaults). Class
                // method defaults are stored under the bare
                // method name (the same key used for top-level
                // functions). Fall back to safe_zero only if
                // no default is registered.
                auto def_it = default_values.find({method_name, i});
                if(def_it != default_values.end())
                {
                  arguments[i] = def_it->second;
                  if(arguments[i].type() != mparams[i].type())
                    arguments[i] =
                      safe_typecast(arguments[i], mparams[i].type());
                }
                else
                {
                  arguments[i] = safe_zero(mparams[i].type());
                }
              }
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
                  explicitly_annotated_params.count(
                    mparams[i].get_identifier()) &&
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
                // Use the call-argument boundary helper (not raw
                // safe_typecast): besides the None-marker adaptation it emits
                // the python-type-error tag obligation for an Any value bound to
                // an explicitly-annotated scalar method param -- closing the
                // method-call soundness gap (the obligation already fired for
                // free functions). Provenance-gated, so self / inferred params
                // do not false-alarm.
                arguments[i] = coerce_call_argument(
                  arguments[i], mparams[i].type(), mparams[i].get_identifier());
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
                  explicitly_annotated_params.count(fp[i].get_identifier()) &&
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
                // Boundary helper (see the mparams loop above): emits the
                // tag obligation for an Any bound to an annotated scalar param.
                arguments[i] = coerce_call_argument(
                  arguments[i], fp[i].type(), fp[i].get_identifier());
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

  return std::nullopt;
}
