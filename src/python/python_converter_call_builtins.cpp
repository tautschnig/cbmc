/// Python to GOTO converter — built-in function dispatch
/// (PLR §6.3.4). Handles the free-function builtins:
/// map, zip, filter, iter, next, len, int, float, bool,
/// print, input, hex, oct, bin, repr, ascii, hash, chr,
/// ord, complex, dict, set, list, reversed, enumerate,
/// sorted, sum, range, round, divmod, str, all, any,
/// hasattr, callable, type, isinstance, abs, min, max.
/// Extracted from python_converter_call.cpp per
/// doc/python-frontend-call-refactor-plan.md (Phase 3.5).
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

#include "python_complex_parser.h"
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
#include <optional>
#include <sstream>
#include <string>

std::optional<exprt> python_convertert::try_builtin_call(
  const jsont &expr,
  const std::string &func_name,
  const jsont &args)
{
  if(func_name == "map")
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

        // PLR §6.1: initialise the result list to all zeros first
        // so out-of-range slots (>= src_len) compare equal to a
        // literal list with trailing zeros (`squared == [1, 4, 9,
        // 16, 25]`). Without this, the per-iteration guarded
        // assigns leave [src_len..MAX) at nondet, breaking
        // struct-equality assertions.
        pending_checks.push_back(
          code_frontend_assignt{tmp, safe_zero(result_list_type)});

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
    {
      // PLR §6.10: 'TypeError: 'NoneType' object is not
      // iterable'. iter(None) raises TypeError. Mirrors the
      // None-callable / None-subscript / len(None) shape.
      exprt arg = convert_expression(*as_array(args).begin());
      if(!arg.is_nil() && is_python_none(arg, symbol_table))
      {
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
            long h = exception_type_hash("TypeError");
            pending_checks.push_back(code_frontend_assignt{
              exc_type_sym->symbol_expr(),
              from_integer(h, exc_type_sym->type)});
          }
        }
        return side_effect_expr_nondett{
          python_value_type(), get_location(expr)};
      }
      return arg;
    }
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  else if(func_name == "next")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      const jsont &arg_ast = *as_array(args).begin();
      exprt arg = convert_expression(arg_ast);
      // PLR §6.10: 'TypeError: 'NoneType' object is not an
      // iterator'. next(None) raises TypeError. Same shape as
      // iter(None) above.
      if(!arg.is_nil() && is_python_none(arg, symbol_table))
      {
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
            long h = exception_type_hash("TypeError");
            pending_checks.push_back(code_frontend_assignt{
              exc_type_sym->symbol_expr(),
              from_integer(h, exc_type_sym->type)});
          }
        }
        return side_effect_expr_nondett{
          python_value_type(), get_location(expr)};
      }
      if(!arg.is_nil() && is_python_list_type(arg.type()))
      {
        const auto &data_type =
          to_array_type(to_struct_type(arg.type()).components()[1].type());

        // PLR §6.2.9: when the argument is a known generator
        // instance (Name bound to a `gen()` call), advance its
        // hidden cursor and raise StopIteration once the cursor
        // reaches the eager-yield list's length. Otherwise fall
        // back to the conservative "return data[0]" approximation
        // so existing list-as-iterator usages keep working.
        irep_idt cursor_id;
        if(is_node_type(arg_ast, "Name"))
        {
          std::string nm = json_string(json_member(arg_ast, "id"));
          irep_idt sid{qualify_name(nm)};
          auto it = generator_cursors.find(sid);
          if(it != generator_cursors.end())
            cursor_id = it->second;
        }
        if(!cursor_id.empty() && symbol_table.lookup(cursor_id) != nullptr)
        {
          symbol_exprt cursor =
            symbol_table.lookup_ref(cursor_id).symbol_expr();
          member_exprt length{arg, "length", signedbv_typet{64}};
          member_exprt data{arg, "data", data_type};

          const symbolt *exc_sym =
            symbol_table.lookup("python::__exception_active");
          const symbolt *exc_type_sym =
            symbol_table.lookup("python::__exception_type");

          // Pre-action: if(cursor >= length) raise StopIteration;
          // else cursor++.
          if(exc_sym != nullptr && exc_type_sym != nullptr)
          {
            exprt cond = binary_relation_exprt{cursor, ID_ge, length};
            code_blockt then_block;
            then_block.add(
              code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
            long h = exception_type_hash("StopIteration");
            then_block.add(code_frontend_assignt{
              exc_type_sym->symbol_expr(), from_integer(h, python_int_type())});
            code_blockt else_block;
            else_block.add(code_frontend_assignt{
              cursor, plus_exprt{cursor, from_integer(1, signedbv_typet{64})}});
            code_ifthenelset advance{
              cond, std::move(then_block), std::move(else_block)};
            advance.add_source_location() = get_location(expr);
            pending_checks.push_back(std::move(advance));
          }
          else
          {
            // Exception infra missing — at least advance the
            // cursor so two next() calls return distinct slots.
            pending_checks.push_back(code_frontend_assignt{
              cursor, plus_exprt{cursor, from_integer(1, signedbv_typet{64})}});
          }

          // The yielded value is the element at the cursor's
          // pre-increment position. Index by (cursor - 1) so the
          // increment that just ran resolves to the right slot.
          // When the StopIteration branch fires, the returned
          // value is unused; (cursor-1) clamps to the last
          // populated slot which is sound for verification.
          exprt prev = minus_exprt{cursor, from_integer(1, signedbv_typet{64})};
          // Guard against negative indexing on the first nondet
          // path: max(cursor - 1, 0).
          if_exprt safe_idx{
            binary_relation_exprt{
              prev, ID_lt, from_integer(0, signedbv_typet{64})},
            from_integer(0, signedbv_typet{64}),
            prev};
          return index_exprt{data, safe_idx};
        }

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
        // PLR §6.10: len(None) raises TypeError. Set
        // __exception_active=True with TypeError tag and emit
        // a nondet result so downstream code doesn't dereference
        // a NULL or read garbage. Mirrors the iter-None /
        // ordering-with-None TypeError emission shape.
        bool none_arg = is_python_none(arg, symbol_table);
        if(none_arg)
        {
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
              long h = exception_type_hash("TypeError");
              pending_checks.push_back(code_frontend_assignt{
                exc_type_sym->symbol_expr(),
                from_integer(h, exc_type_sym->type)});
            }
          }
          return side_effect_expr_nondett{
            python_int_type(), get_location(expr)};
        }
        // Python string: route through the refinement intrinsic
        // so the back-end (refine-strings or future SMT strings)
        // controls the semantics.
        if(is_python_string_type(arg.type()))
        {
          // PLR §6.2.4 / §6.10.2: constant-fold len(s) when the
          // string content is known (literal, tracked symbol via
          // string_constants, or struct literal). Avoids a
          // string-solver round-trip and lets downstream
          // constant-fold paths (subscript, predicates) fire.
          auto sv = extract_string_value(arg);
          if(sv.has_value())
          {
            // PLR §6.10: len(s) returns the code-point count.
            // Count UTF-8 leading bytes (non-continuation).
            long long cp = 0;
            for(char c : sv.value())
            {
              unsigned char uc = static_cast<unsigned char>(c);
              if(uc < 0x80 || uc >= 0xC0)
                ++cp;
            }
            return from_integer(cp, python_int_type());
          }
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

        // PLR builtins: int(x, base=10). Parse the optional second
        // positional `base` argument; supported bases are 0
        // (auto-detect via 0x/0b/0o prefix) and 2..36.
        int parse_base = 10;
        bool have_base = as_array(args).size() >= 2;
        if(have_base)
        {
          exprt b_arg = convert_expression(*std::next(as_array(args).begin()));
          if(b_arg.is_constant() && b_arg.type().id() == ID_signedbv)
          {
            mp_integer bv;
            if(!to_integer(to_constant_expr(b_arg), bv))
              parse_base = bv.to_long();
          }
        }
        // int("60") — parse constant string to int.
        if(is_python_string_type(arg.type()))
        {
          auto sv = extract_string_value(arg);
          if(sv.has_value())
          {
            // Strip ASCII whitespace from both ends per CPython.
            std::string trimmed = sv.value();
            std::size_t a = 0;
            while(a < trimmed.size() &&
                  std::isspace(static_cast<unsigned char>(trimmed[a])))
              a++;
            std::size_t b = trimmed.size();
            while(b > a &&
                  std::isspace(static_cast<unsigned char>(trimmed[b - 1])))
              b--;
            std::string body = trimmed.substr(a, b - a);
            // Optional sign.
            std::string sign;
            if(!body.empty() && (body[0] == '+' || body[0] == '-'))
            {
              sign = body.substr(0, 1);
              body = body.substr(1);
            }
            // PLR §6.4.4: int(x, 0) auto-detects the base from a
            // prefix (0x/0X => 16, 0b/0B => 2, 0o/0O => 8) and
            // falls back to base 10 when no prefix is present.
            // For an explicit base, Python accepts the matching
            // prefix and strips it before parsing.
            int effective_base = parse_base;
            if(effective_base == 0)
            {
              if(
                body.size() >= 2 && body[0] == '0' &&
                (body[1] == 'x' || body[1] == 'X'))
              {
                effective_base = 16;
                body = body.substr(2);
              }
              else if(
                body.size() >= 2 && body[0] == '0' &&
                (body[1] == 'b' || body[1] == 'B'))
              {
                effective_base = 2;
                body = body.substr(2);
              }
              else if(
                body.size() >= 2 && body[0] == '0' &&
                (body[1] == 'o' || body[1] == 'O'))
              {
                effective_base = 8;
                body = body.substr(2);
              }
              else
              {
                effective_base = 10;
              }
            }
            else if(
              effective_base == 16 && body.size() >= 2 && body[0] == '0' &&
              (body[1] == 'x' || body[1] == 'X'))
              body = body.substr(2);
            else if(
              effective_base == 2 && body.size() >= 2 && body[0] == '0' &&
              (body[1] == 'b' || body[1] == 'B'))
              body = body.substr(2);
            else if(
              effective_base == 8 && body.size() >= 2 && body[0] == '0' &&
              (body[1] == 'o' || body[1] == 'O'))
              body = body.substr(2);

            errno = 0;
            char *endp = nullptr;
            std::string full = sign + body;
            long long val = std::strtoll(full.c_str(), &endp, effective_base);
            if(!body.empty() && endp == full.c_str() + full.size())
              return from_integer(val, python_int_type());
            // Constant string that can't be parsed: surface a
            // ValueError.
            add_check(
              false_exprt{},
              "exception",
              "ValueError: invalid literal for int() with base " +
                std::to_string(parse_base),
              get_location(expr));
            return side_effect_expr_nondett{
              python_int_type(), get_location(expr)};
          }
          // Symbolic string: emit cprover_string_parse_int_func
          // so the solver knows the int's relationship to the
          // string's characters.
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
            ieee_floatt fv{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            // strtod is exception-free; std::stod throws on
            // out-of-range denormals like "1e-308" (libstdc++ quirk)
            // and on unparseable inputs.
            errno = 0;
            char *endp = nullptr;
            const double v = std::strtod(sv->c_str(), &endp);
            if(endp == sv->c_str() + sv->size())
            {
              fv.from_double(v);
              return fv.to_expr();
            }
            // Fall through to the nondet path below for malformed
            // input — Python's float() would raise ValueError, but
            // detecting that statically is the caller's job.
          }
        }
        // float(<python_value>) — extract the tagged-union's
        // float slot. Avoids CBMC's smt2_conv struct_tag→float
        // typecast fallback (which UNEXPECTEDCASEs on cvc5).
        if(is_python_value_type(arg.type()))
          return python_value_float(arg);
        // float(<other struct>): return a nondet float over-
        // approximation rather than letting smt2_conv's
        // typecast handler abort.
        if(arg.type().id() == ID_struct || arg.type().id() == ID_struct_tag)
        {
          log_overapprox("float() on opaque struct — returning nondet float");
          return side_effect_expr_nondett{double_type(), source_locationt{}};
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
        return python_truthiness(arg);
      }
    }
    return false_exprt{};
  }
  // print() — return None (modeled as a sentinel int). The
  // arguments aren't observed by the verifier, but they ARE
  // evaluated and assigned to a discard-temp so any embedded
  // checks (overflow on a+b, KeyError on d[k], etc.) fire at
  // the call site.
  else if(func_name == "print")
  {
    if(args.is_array())
    {
      static unsigned print_arg_ctr = 0;
      for(const auto &a : as_array(args))
      {
        exprt v = convert_expression(a);
        if(v.is_nil())
          continue;
        std::string tn = "__print_arg_" + std::to_string(print_arg_ctr++);
        std::string tq = qualify_name(tn);
        irep_idt tid{tq};
        if(symbol_table.lookup(tid) == nullptr)
        {
          symbolt s{tid, v.type(), "python"};
          s.base_name = tn;
          s.is_lvalue = true;
          s.is_state_var = true;
          symbol_table.add(s);
        }
        // Re-fetch in case lookup_ref updates type
        const symbolt &ts = symbol_table.lookup_ref(tid);
        symbol_exprt te = ts.symbol_expr();
        // Assign so goto-instrument's overflow / KeyError checks
        // see the embedded sub-expressions.
        if(v.type() != te.type())
          v = safe_typecast(v, te.type());
        pending_checks.push_back(code_frontend_assignt{te, v});
      }
    }
    mp_integer none_val = python_none_sentinel_int();
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
      // PLR §6.10.1: repr(complex) follows the same format as
      // str(complex) — both produce '(real+imagj)' or 'Nj' for
      // pure-imag — so dispatch to the same constant-fold path.
      if(!arg.is_nil() && arg.type().id() == ID_struct)
      {
        const auto &tag = to_struct_type(arg.type()).get_tag();
        if(id2string(tag) == "python_complex")
        {
          // Try to recover the (real, imag) pair from the
          // struct directly or via complex_literals.
          std::optional<double> re, im;
          if(arg.id() == ID_struct && arg.operands().size() >= 2)
          {
            re = try_eval_double(arg.operands()[0]);
            im = try_eval_double(arg.operands()[1]);
          }
          if((!re.has_value() || !im.has_value()) && arg.id() == ID_symbol)
          {
            auto sid = to_symbol_expr(arg).get_identifier();
            auto it = complex_literals.find(sid);
            if(
              it != complex_literals.end() && it->second.id() == ID_struct &&
              it->second.operands().size() >= 2)
            {
              re = try_eval_double(it->second.operands()[0]);
              im = try_eval_double(it->second.operands()[1]);
            }
          }
          if(re.has_value() && im.has_value())
          {
            double r = re.value(), i = im.value();
            auto fmt = [](double d) -> std::string
            {
              if(d == std::floor(d) && std::abs(d) < 1e15)
                return std::to_string(static_cast<long long>(d));
              std::ostringstream oss;
              oss << d;
              std::string s = oss.str();
              if(s.find('.') != std::string::npos)
                while(s.size() > 1 && s.back() == '0' && s[s.size() - 2] != '.')
                  s.pop_back();
              return s;
            };
            std::string out;
            if(r == 0.0 && i == 0.0)
              out = "0j";
            else if(r == 0.0)
              out = fmt(i) + "j";
            else
            {
              std::string sr = fmt(r);
              std::string sep = i >= 0 ? "+" : "-";
              std::string si = fmt(std::fabs(i));
              out = "(" + sr + sep + si + "j)";
            }
            return python_string_literal(out);
          }
        }
        std::string tagstr = id2string(to_struct_type(arg.type()).get_tag());
        std::string cls =
          tagstr.substr(0, 13) == "python_class_" ? tagstr.substr(13) : tagstr;
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
      // PLR §builtins: chr(i) requires an integer argument. A float
      // argument (e.g. chr(66.9)) raises TypeError. Emit the check
      // before any range check, and reject any non-integer numeric
      // type at conversion time.
      if(code_point.type().id() == ID_floatbv)
      {
        add_check(
          false_exprt{},
          "exception",
          "TypeError: an integer is required",
          get_location(expr));
      }
      // PLR §builtins: chr(i) raises ValueError when i is
      // outside [0, 0x10ffff]. Route through the
      // __exception_active model so try/except can catch it
      // (matches the test pattern `try: chr(-1); except
      // ValueError: pass`). Previously emitted as a property
      // check, which made the test report verification failed
      // even when the user explicitly handled the exception.
      {
        const symbolt *exc_sym =
          symbol_table.lookup("python::__exception_active");
        const symbolt *exc_type_sym =
          symbol_table.lookup("python::__exception_type");
        if(exc_sym != nullptr && exc_type_sym != nullptr)
        {
          exprt out_of_range = or_exprt{
            binary_relation_exprt{
              code_point, ID_lt, from_integer(0, code_point.type())},
            binary_relation_exprt{
              code_point, ID_gt, from_integer(0x10ffff, code_point.type())}};
          code_blockt set_exc;
          set_exc.add(
            code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
          long h = exception_type_hash("ValueError");
          set_exc.add(code_frontend_assignt{
            exc_type_sym->symbol_expr(), from_integer(h, python_int_type())});
          code_ifthenelset cond_raise{out_of_range, std::move(set_exc)};
          cond_raise.add_source_location() = get_location(expr);
          pending_checks.push_back(std::move(cond_raise));
        }
      }
      typet str_type = python_string_type();
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

    // Helper: turn an argument expression into a (real, imag)
    // pair of double exprts, consistent with PLR §6.10.1's
    // complex constructor semantics:
    //   - complex c → (c.real, c.imag)
    //   - bool / int / float / unwrapped numeric → (value, 0)
    //   - constant string "1+2j" → parsed (real, imag); only
    //     valid as the sole positional arg per CPython, but
    //     we accept it here too for the keyword-form
    //     'complex(real="...")'.
    auto to_complex_parts = [&](const exprt &arg) -> std::pair<exprt, exprt>
    {
      // python_complex struct → unpack fields directly.
      if(
        arg.type().id() == ID_struct &&
        to_struct_type(arg.type()).get_tag() == "python_complex")
      {
        return {
          member_exprt{arg, "real", double_type()},
          member_exprt{arg, "imag", double_type()}};
      }
      // Float: (value, 0).
      if(arg.type().id() == ID_floatbv)
        return {arg, safe_zero(double_type())};
      // Bool / int / unwrapped numeric: try_eval_double then
      // fall back to constant typecast.
      auto ev = try_eval_double(arg);
      ieee_floatt fv{
        ieee_float_spect::double_precision(),
        ieee_floatt::rounding_modet::ROUND_TO_EVEN};
      if(ev.has_value())
      {
        fv.from_double(ev.value());
        return {fv.to_expr(), safe_zero(double_type())};
      }
      if(arg.is_constant())
      {
        mp_integer iv;
        if(!to_integer(to_constant_expr(arg), iv))
        {
          fv.from_integer(iv);
          return {fv.to_expr(), safe_zero(double_type())};
        }
      }
      // Symbolic numeric: typecast to double for the real part.
      if(
        arg.type().id() == ID_signedbv || arg.type().id() == ID_unsignedbv ||
        arg.type().id() == ID_bool || arg.type().id() == ID_integer)
      {
        return {safe_typecast(arg, double_type()), safe_zero(double_type())};
      }
      // Fallback: zero.
      return {safe_zero(double_type()), safe_zero(double_type())};
    };

    exprt real_val = safe_zero(double_type());
    exprt imag_val = safe_zero(double_type());
    bool imag_is_complex = false;
    if(args.is_array())
    {
      auto it = as_array(args).begin();
      if(it != as_array(args).end())
      {
        exprt arg = convert_expression(*it);
        // PLR §6.10.1: complex(str) parses a string like
        // "1+2j" / "(1-2j)" into the python_complex struct.
        // Only valid as the sole positional arg.
        if(is_python_string_type(arg.type()) && is_node_type(*it, "Constant"))
        {
          const jsont &cv = json_member(*it, "value");
          if(cv.is_string())
          {
            std::string sv = cv.value;
            if(auto cv_pair = parse_python_complex_string(sv);
               cv_pair.has_value())
            {
              ieee_floatt real_f{
                ieee_float_spect::double_precision(),
                ieee_floatt::rounding_modet::ROUND_TO_EVEN};
              real_f.from_double(cv_pair->first);
              ieee_floatt imag_f{
                ieee_float_spect::double_precision(),
                ieee_floatt::rounding_modet::ROUND_TO_EVEN};
              imag_f.from_double(cv_pair->second);
              return struct_exprt{
                {real_f.to_expr(), imag_f.to_expr()}, complex_type};
            }
            // Malformed string → ValueError, matching CPython.
            emit_value_error(false_exprt{});
            return side_effect_expr_nondett{complex_type, get_location(expr)};
          }
        }
        auto parts = to_complex_parts(arg);
        // Default contribution to result: real += parts.real,
        // imag += parts.imag (initial). Combined below with
        // the second arg.
        real_val = parts.first;
        imag_val = parts.second;
        ++it;
      }
      if(it != as_array(args).end())
      {
        exprt arg = convert_expression(*it);
        if(
          arg.type().id() == ID_struct &&
          to_struct_type(arg.type()).get_tag() == "python_complex")
        {
          imag_is_complex = true;
        }
        auto parts = to_complex_parts(arg);
        // PLR §6.10.1: result = real_arg + imag_arg * j.
        // For real_arg = a + bj (complex) and imag_arg = c + dj
        // (complex): result = a + bj + (c + dj)*j = (a - d) +
        // (b + c)j.
        // For non-complex imag_arg = c (d=0): result = a + (b+c)j.
        // For complex imag_arg only: result.real -= imag.imag,
        // result.imag += imag.real.
        // For non-complex imag_arg: result.imag += imag.real.
        if(imag_is_complex)
        {
          // real -= imag.imag
          real_val = minus_exprt{real_val, parts.second};
          // imag += imag.real
          imag_val = plus_exprt{imag_val, parts.first};
        }
        else
        {
          // imag += imag (which is parts.first, the real part of
          // the second arg's number).
          imag_val = plus_exprt{imag_val, parts.first};
        }
      }
    }
    // PLR §6.10.1: complex(real=N, imag=M) — keyword-only
    // call. The constructor accepts 'real' and 'imag' as
    // named keywords (only with no positional second arg).
    // Walk the call's keywords and replace real_val /
    // imag_val from any matches.
    {
      const jsont &kws = json_member(expr, "keywords");
      if(kws.is_array())
      {
        bool kw_imag_is_complex = false;
        std::pair<exprt, exprt> kw_real_parts{
          safe_zero(double_type()), safe_zero(double_type())};
        std::pair<exprt, exprt> kw_imag_parts{
          safe_zero(double_type()), safe_zero(double_type())};
        bool have_kw_real = false;
        bool have_kw_imag = false;
        for(const auto &kw : as_array(kws))
        {
          std::string kn = json_string(json_member(kw, "arg"));
          if(kn != "real" && kn != "imag")
            continue;
          const jsont &kw_val_node = json_member(kw, "value");
          // String 'real="1+2j"' — parse like the positional
          // string-arg path.
          if(kn == "real" && is_node_type(kw_val_node, "Constant"))
          {
            const jsont &cv = json_member(kw_val_node, "value");
            if(cv.is_string())
            {
              if(auto cv_pair = parse_python_complex_string(cv.value);
                 cv_pair.has_value())
              {
                ieee_floatt real_f{
                  ieee_float_spect::double_precision(),
                  ieee_floatt::rounding_modet::ROUND_TO_EVEN};
                real_f.from_double(cv_pair->first);
                ieee_floatt imag_f{
                  ieee_float_spect::double_precision(),
                  ieee_floatt::rounding_modet::ROUND_TO_EVEN};
                imag_f.from_double(cv_pair->second);
                kw_real_parts = {real_f.to_expr(), imag_f.to_expr()};
                have_kw_real = true;
                continue;
              }
            }
          }
          exprt kv = convert_expression(kw_val_node);
          bool kv_is_complex =
            (kv.type().id() == ID_struct &&
             to_struct_type(kv.type()).get_tag() == "python_complex");
          auto parts = to_complex_parts(kv);
          if(kn == "real")
          {
            kw_real_parts = parts;
            have_kw_real = true;
          }
          else
          {
            kw_imag_parts = parts;
            kw_imag_is_complex = kv_is_complex;
            have_kw_imag = true;
          }
        }
        if(have_kw_real || have_kw_imag)
        {
          // Restart the computation with kw values overriding
          // any positional defaults that came before.
          real_val =
            have_kw_real ? kw_real_parts.first : safe_zero(double_type());
          imag_val =
            have_kw_real ? kw_real_parts.second : safe_zero(double_type());
          if(have_kw_imag)
          {
            if(kw_imag_is_complex)
            {
              real_val = minus_exprt{real_val, kw_imag_parts.second};
              imag_val = plus_exprt{imag_val, kw_imag_parts.first};
            }
            else
            {
              imag_val = plus_exprt{imag_val, kw_imag_parts.first};
            }
          }
        }
      }
    }
    return struct_exprt{{real_val, imag_val}, complex_type};
  }
  // PLR §8.5: collections.defaultdict / Counter constructor.
  // Either form:
  //   defaultdict(factory)            - bare-name from `from collections
  //                                     import defaultdict`
  //   collections.defaultdict(factory) - module-qualified
  //   col.defaultdict(factory)        - module alias
  // We model both as an empty dict, and remember the receiving
  // variable's factory so subsequent missing-key reads return
  // the factory's zero value instead of raising KeyError.
  // (defaultdict_factories registration happens in convert_assign
  // because we don't know the assignment target here; see the
  // _defaultdict_factory_pending hint we attach to the result.)
  else if(
    collections_imports.count(func_name) > 0 &&
    (collections_imports[func_name] == "defaultdict" ||
     collections_imports[func_name] == "Counter"))
  {
    // Determine factory and corresponding value type.
    std::string factory;
    if(collections_imports[func_name] == "Counter")
      factory = "int"; // Counter values default to 0
    else if(args.is_array() && !as_array(args).empty())
    {
      const jsont &fa = *as_array(args).begin();
      if(is_node_type(fa, "Name"))
        factory = json_string(json_member(fa, "id"));
      else if(is_node_type(fa, "Constant"))
      {
        const jsont &cv = json_member(fa, "value");
        if(cv.is_null())
          factory = ""; // defaultdict(None) → plain dict
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
          kval = coerce_element(kval, vals_arr_type.element_type());
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
      // PLR §6.10.2: set(constant_string) — fold to a bitmap
      // with one bit per unique character. The bit positions
      // are arbitrary (we use 0..N-1), since the only ops we
      // support on string-set values are popcount(len) and
      // truthiness (== empty). For larger consumer ops with
      // string-set, fall back to nondet.
      if(is_python_string_type(arg.type()))
      {
        auto sv = extract_string_value(arg);
        if(!sv.has_value() && arg.id() == ID_symbol)
        {
          auto it = string_constants.find(to_symbol_expr(arg).get_identifier());
          if(it != string_constants.end())
            sv = it->second;
        }
        if(sv.has_value())
        {
          std::set<char> uniq(sv.value().begin(), sv.value().end());
          std::size_t n = uniq.size();
          if(n <= 64)
          {
            // Build bitmap with bits 0..n-1 set: (1 << n) - 1
            // (using uint128 trick for n=64).
            std::uint64_t bm = n == 64
                                 ? ~static_cast<std::uint64_t>(0)
                                 : ((static_cast<std::uint64_t>(1) << n) - 1);
            return struct_exprt{
              {from_integer(bm, unsignedbv_typet{64}),
               from_integer(0, signedbv_typet{64})},
              python_set_type()};
          }
        }
      }
      // PLR §6.10.2: set(list_of_ints) — build a bitmap-set so it
      // interoperates with set literals {1,2,3} and the binary
      // operators (|, &, -, ^). For non-int element types we
      // fall through to the legacy list-backed path.
      if(
        is_python_list_type(arg.type()) &&
        to_array_type(to_struct_type(arg.type()).components()[1].type())
            .element_type()
            .id() == ID_signedbv)
      {
        const auto &list_st = to_struct_type(arg.type());
        const auto &data_type = to_array_type(list_st.components()[1].type());
        member_exprt src_data{arg, "data", data_type};
        member_exprt src_len{arg, "length", signedbv_typet{64}};

        // Build the bitmap by OR-ing 1 << data[i] for each
        // i in [0, length).
        static unsigned bm_set_ctr = 0;
        std::string bm_name = "__set_bm_" + std::to_string(bm_set_ctr++);
        std::string bm_qname = qualify_name(bm_name);
        irep_idt bm_id{bm_qname};
        if(symbol_table.lookup(bm_id) == nullptr)
        {
          symbolt bm_sym{bm_id, unsignedbv_typet{64}, "python"};
          bm_sym.base_name = bm_name;
          bm_sym.is_lvalue = true;
          bm_sym.is_state_var = true;
          symbol_table.add(bm_sym);
        }
        symbol_exprt bm = symbol_table.lookup_ref(bm_id).symbol_expr();
        pending_checks.push_back(
          code_frontend_assignt{bm, from_integer(0, unsignedbv_typet{64})});
        for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
        {
          exprt idx = from_integer(i, signedbv_typet{64});
          exprt elem = index_exprt{src_data, idx};
          // shift = 1 << elem (as unsigned 64).
          exprt shift_amt = typecast_exprt{elem, unsignedbv_typet{64}};
          exprt one_shifted =
            shl_exprt{from_integer(1, unsignedbv_typet{64}), shift_amt};
          pending_checks.push_back(code_ifthenelset{
            binary_relation_exprt{idx, ID_lt, src_len},
            code_frontend_assignt{bm, bitor_exprt{bm, one_shifted}}});
        }
        return struct_exprt{
          {bm, from_integer(0, signedbv_typet{64})}, python_set_type()};
      }
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
    // PLR §6.10.2: set() with no argument (or with a non-list
    // argument we can't statically expand) returns an empty set.
    // Return an empty python_set_type struct (bitmap=0, offset=0)
    // so set-op and set-compare fast paths fire (they require
    // python_set_type on both sides).
    {
      typet st_t = python_set_type();
      // Build a constant struct with the underlying field types.
      // python_set_struct_def() gives us { bitmap: u64, offset: i64 }.
      const auto &set_st = python_set_struct_def();
      exprt::operandst zeros;
      for(const auto &c : set_st.components())
        zeros.push_back(safe_zero(c.type()));
      return struct_exprt{std::move(zeros), st_t};
    }
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
  // enumerate(iterable, start=0) → list of (start+i, element) tuples
  else if(func_name == "enumerate")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      auto arg_it = as_array(args).begin();
      exprt arg = convert_expression(*arg_it);

      // Parse the optional second positional argument (start) and
      // the start=... keyword. Default is 0. PLib §enumerate.
      mp_integer start_val{0};
      ++arg_it;
      if(arg_it != as_array(args).end())
      {
        exprt s = convert_expression(*arg_it);
        if(s.is_constant())
        {
          mp_integer v;
          if(!to_integer(to_constant_expr(s), v))
            start_val = v;
        }
      }
      const jsont &enum_kw = json_member(expr, "keywords");
      if(enum_kw.is_array())
      {
        for(const auto &k : as_array(enum_kw))
        {
          if(json_string(json_member(k, "arg")) == "start")
          {
            exprt s = convert_expression(json_member(k, "value"));
            if(s.is_constant())
            {
              mp_integer v;
              if(!to_integer(to_constant_expr(s), v))
                start_val = v;
            }
          }
        }
      }

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
            {from_integer(start_val + mp_integer{i}, python_int_type()),
             index_exprt{data, idx}},
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
            // Try int elements first, fall back to string elements.
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
                {
                  all_const_int = false;
                }
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
            if(all_const_int && !int_pairs.empty())
            {
              if(sorted_reverse)
                std::sort(
                  int_pairs.begin(),
                  int_pairs.end(),
                  [](const auto &a, const auto &b)
                  { return a.first > b.first; });
              else
                std::sort(
                  int_pairs.begin(),
                  int_pairs.end(),
                  [](const auto &a, const auto &b)
                  { return a.first < b.first; });
              const auto &list_st = to_struct_type(arg.type());
              const auto &data_type =
                to_array_type(list_st.components()[1].type());
              exprt::operandst sorted_elems;
              for(const auto &p : int_pairs)
                sorted_elems.push_back(p.second);
              while(sorted_elems.size() < PYTHON_MAX_LIST_LENGTH)
                sorted_elems.push_back(safe_zero(data_type.element_type()));
              return struct_exprt{
                {lit->operands()[0],
                 array_exprt{std::move(sorted_elems), data_type}},
                arg.type()};
            }
            // PLR §6.10: when all elements are constant strings,
            // sort lexicographically. Avoids the runtime
            // bubble-sort that would emit O(n²) string-solver
            // comparisons and time out.
            if(all_const_str && !str_pairs.empty())
            {
              if(sorted_reverse)
                std::sort(
                  str_pairs.begin(),
                  str_pairs.end(),
                  [](const auto &a, const auto &b)
                  { return a.first > b.first; });
              else
                std::sort(
                  str_pairs.begin(),
                  str_pairs.end(),
                  [](const auto &a, const auto &b)
                  { return a.first < b.first; });
              const auto &list_st = to_struct_type(arg.type());
              const auto &data_type =
                to_array_type(list_st.components()[1].type());
              exprt::operandst sorted_elems;
              for(const auto &p : str_pairs)
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
      // PLR §6.10.2: sorted() on a bitmap-int-set materialises
      // the set's elements as a list, then sorts. We iterate
      // bits 0..63 of the bitmap and fill the list with the
      // bits that are set (already in numeric order, so the
      // forward pass produces a sorted list).
      if(is_python_set_type(arg.type()))
      {
        member_exprt bm{arg, "bitmap", unsignedbv_typet{64}};
        member_exprt off{arg, "offset", signedbv_typet{64}};
        typet lt = python_list_type(python_int_type());
        const auto &slist_st = to_struct_type(lt);
        const auto &sdata_type = to_array_type(slist_st.components()[1].type());
        static unsigned setsort_ctr = 0;
        std::string tn = "__set_sort_" + std::to_string(setsort_ctr++);
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
        symbol_exprt stmp = symbol_table.lookup_ref(ti).symbol_expr();
        member_exprt sdata{stmp, "data", sdata_type};
        member_exprt slength{stmp, "length", signedbv_typet{64}};
        // PLR §6.10.1: zero-init the data buffer so positions
        // beyond the materialised length match a literal's
        // trailing zeros at struct-equality time.
        {
          exprt::operandst zeros;
          while(zeros.size() < PYTHON_MAX_LIST_LENGTH)
            zeros.push_back(safe_zero(sdata_type.element_type()));
          pending_checks.push_back(code_frontend_assignt{
            sdata, array_exprt{std::move(zeros), sdata_type}});
        }
        pending_checks.push_back(
          code_frontend_assignt{slength, from_integer(0, signedbv_typet{64})});
        for(std::size_t bit = 0; bit < 64; bit++)
        {
          exprt bit_set = notequal_exprt{
            bitand_exprt{
              bm, from_integer(mp_integer{1} << bit, unsignedbv_typet{64})},
            from_integer(0, unsignedbv_typet{64})};
          exprt val = plus_exprt{
            off, from_integer(static_cast<long long>(bit), signedbv_typet{64})};
          if(val.type() != sdata_type.element_type())
            val = coerce_element(val, sdata_type.element_type());
          code_blockt append;
          append.add(code_frontend_assignt{index_exprt{sdata, slength}, val});
          append.add(code_frontend_assignt{
            slength, plus_exprt{slength, from_integer(1, signedbv_typet{64})}});
          pending_checks.push_back(
            code_ifthenelset{bit_set, std::move(append)});
        }
        if(sorted_reverse)
        {
          for(std::size_t i = 0; i < 32; i++)
          {
            exprt il = from_integer(i, signedbv_typet{64});
            exprt ir = minus_exprt{
              minus_exprt{slength, from_integer(1, signedbv_typet{64})}, il};
            exprt do_swap = binary_relation_exprt{il, ID_lt, ir};
            static unsigned swap_ctr = 0;
            std::string sn = "__set_sort_swap_" + std::to_string(swap_ctr++);
            std::string sq = qualify_name(sn);
            irep_idt si{sq};
            if(symbol_table.lookup(si) == nullptr)
            {
              symbolt ss{si, sdata_type.element_type(), "python"};
              ss.base_name = sn;
              ss.is_lvalue = true;
              ss.is_state_var = true;
              symbol_table.add(ss);
            }
            symbol_exprt sv = symbol_table.lookup_ref(si).symbol_expr();
            code_blockt swap;
            swap.add(code_frontend_assignt{sv, index_exprt{sdata, il}});
            swap.add(code_frontend_assignt{
              index_exprt{sdata, il}, index_exprt{sdata, ir}});
            swap.add(code_frontend_assignt{index_exprt{sdata, ir}, sv});
            pending_checks.push_back(
              code_ifthenelset{do_swap, std::move(swap)});
          }
        }
        return std::move(stmp);
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

        // PLR §6.2.5: if the list element type is python_value
        // (typical for *args), unwrap each element to its
        // numeric content. Otherwise accumulate at the element
        // type directly.
        bool elem_is_value = is_python_value_type(data_type.element_type());
        typet acc_type =
          elem_is_value ? python_int_type() : data_type.element_type();

        // Unrolled accumulation: result = sum of data[0..length-1]
        static unsigned sum_counter = 0;
        std::string tmp_name = "__sum_" + std::to_string(sum_counter++);
        std::string tmp_qname = qualify_name(tmp_name);
        irep_idt tmp_id{tmp_qname};
        if(symbol_table.lookup(tmp_id) == nullptr)
        {
          symbolt tmp_sym{tmp_id, acc_type, "python"};
          tmp_sym.base_name = tmp_name;
          tmp_sym.is_lvalue = true;
          tmp_sym.is_state_var = true;
          symbol_table.add(tmp_sym);
        }
        symbol_exprt tmp = symbol_table.lookup_ref(tmp_id).symbol_expr();
        pending_checks.push_back(
          code_frontend_assignt{tmp, from_integer(0, acc_type)});
        for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
        {
          exprt idx = from_integer(i, signedbv_typet{64});
          exprt elem = index_exprt{data, idx};
          if(elem_is_value)
            elem = unwrap_value(elem, acc_type);
          else if(elem.type() != acc_type)
            elem = safe_typecast(elem, acc_type);
          pending_checks.push_back(code_ifthenelset{
            binary_relation_exprt{idx, ID_lt, length},
            code_frontend_assignt{tmp, plus_exprt{tmp, elem}}});
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

      // PLR §6.10.1: zero-init the data buffer so trailing slots
      // beyond the materialised range match a literal's trailing
      // zeros at struct-equality time.
      {
        exprt::operandst zeros;
        while(zeros.size() < PYTHON_MAX_LIST_LENGTH)
          zeros.push_back(safe_zero(data_type.element_type()));
        pending_checks.push_back(code_frontend_assignt{
          data, array_exprt{std::move(zeros), data_type}});
      }

      // Fill: data[i] = start + i * step for i in 0..MAX
      pending_checks.push_back(
        code_frontend_assignt{length, from_integer(0, signedbv_typet{64})});
      for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
      {
        exprt idx = from_integer(i, signedbv_typet{64});
        exprt val = plus_exprt{start, mult_exprt{idx, step}};
        // PLR §6.10.1: positive step iterates while val < stop;
        // negative step while val > stop. Pick at runtime so
        // 'range(5, 0, -1)' produces [5,4,3,2,1] and
        // 'range(2, 5, -1)' produces [].
        exprt step_pos = binary_relation_exprt{
          step, ID_gt, from_integer(0, python_int_type())};
        exprt in_range = if_exprt{
          step_pos,
          binary_relation_exprt{val, ID_lt, stop},
          binary_relation_exprt{val, ID_gt, stop}};
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
      // PLR §6.7: divmod with any float arg → float result
      // (using true division and floor). Detect float BEFORE
      // typecasting to int, otherwise 7.5 becomes 7 and the
      // remainder is wrong.
      bool is_float =
        a.type().id() == ID_floatbv || b.type().id() == ID_floatbv;
      typet result_type = is_float ? double_type() : python_int_type();
      if(is_float)
      {
        a = safe_typecast(a, double_type());
        b = safe_typecast(b, double_type());
      }
      else
      {
        a = safe_typecast(a, python_int_type());
        b = safe_typecast(b, python_int_type());
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
        if(id2string(tag) == "python_complex")
        {
          // PLR §6.10.1: str(complex) follows CPython format:
          //   0+0j → "0j"
          //   real == 0 → "Nj" / "-Nj" (compact form)
          //   imag == 0 → "(N+0j)" / "(-N+0j)"
          //   mixed → "(real+imagj)" / "(real-imagj)"
          // Constant-fold via try_eval_double on the struct fields.
          if(arg.id() == ID_struct && arg.operands().size() >= 2)
          {
            auto re = try_eval_double(arg.operands()[0]);
            auto im = try_eval_double(arg.operands()[1]);
            if(re.has_value() && im.has_value())
            {
              double r = re.value(), i = im.value();
              auto fmt = [](double d) -> std::string
              {
                if(d == std::floor(d) && std::abs(d) < 1e15)
                  return std::to_string(static_cast<long long>(d));
                std::ostringstream oss;
                oss << d;
                std::string s = oss.str();
                if(s.find('.') != std::string::npos)
                  while(s.size() > 1 && s.back() == '0' &&
                        s[s.size() - 2] != '.')
                    s.pop_back();
                return s;
              };
              std::string out;
              if(r == 0.0 && i == 0.0)
                out = "0j";
              else if(r == 0.0)
                out = fmt(i) + "j";
              else
              {
                std::string sr = fmt(r);
                std::string sep = i >= 0 ? "+" : "-";
                std::string si = fmt(std::fabs(i));
                out = "(" + sr + sep + si + "j)";
              }
              return python_string_literal(out);
            }
          }
          // Symbol bound via complex_literals — look up the
          // struct value and recurse.
          if(arg.id() == ID_symbol)
          {
            auto sid = to_symbol_expr(arg).get_identifier();
            auto it = complex_literals.find(sid);
            if(
              it != complex_literals.end() && it->second.id() == ID_struct &&
              it->second.operands().size() >= 2)
            {
              auto re = try_eval_double(it->second.operands()[0]);
              auto im = try_eval_double(it->second.operands()[1]);
              if(re.has_value() && im.has_value())
              {
                double r = re.value(), i = im.value();
                auto fmt = [](double d) -> std::string
                {
                  if(d == std::floor(d) && std::abs(d) < 1e15)
                    return std::to_string(static_cast<long long>(d));
                  std::ostringstream oss;
                  oss << d;
                  std::string s = oss.str();
                  if(s.find('.') != std::string::npos)
                    while(s.size() > 1 && s.back() == '0' &&
                          s[s.size() - 2] != '.')
                      s.pop_back();
                  return s;
                };
                std::string out;
                if(r == 0.0 && i == 0.0)
                  out = "0j";
                else if(r == 0.0)
                  out = fmt(i) + "j";
                else
                {
                  std::string sr = fmt(r);
                  std::string sep = i >= 0 ? "+" : "-";
                  std::string si = fmt(std::fabs(i));
                  out = "(" + sr + sep + si + "j)";
                }
                return python_string_literal(out);
              }
            }
          }
          // Non-constant python_complex (e.g., result of
          // complex arithmetic) — return a placeholder string
          // matching CPython-incompatible-but-test-friendly
          // behaviour. This is a tradeoff: precision-folding
          // for arithmetic results would require carrying
          // through the addition / multiplication symbolically.
          if(arg.id() == ID_struct || arg.id() == ID_symbol)
            return python_string_literal("(complex)");
        }
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
          pending_checks,
          loop_depth > 0);
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
        // 2-level nested generator: `all(EXPR for x in xs for y in ys)`.
        // Both iter sources may be literal lists or Name
        // references that bind to list literals; no var
        // shadowing (xs's loop var != ys's loop var). Unrolls
        // the cartesian product, substitutes both vars in EXPR,
        // and accumulates via AND/OR. Higher-arity nesting and
        // var-shadowing fall through to the single-generator
        // path below.
        if(
          generators.is_array() && as_array(generators).size() == 2 &&
          !as_array(generators).empty())
        {
          // Helper: extract list elements as exprts when iter is
          // a Name binding to a list literal.
          auto fetch_struct_elements =
            [&](const jsont &iter_node) -> std::vector<exprt>
          {
            std::vector<exprt> out;
            if(is_node_type(iter_node, "Name"))
            {
              std::string nm = json_string(json_member(iter_node, "id"));
              irep_idt sid{qualify_name(nm)};
              auto it = list_literals.find(sid);
              if(
                it != list_literals.end() && it->second.id() == ID_struct &&
                it->second.operands().size() >= 2)
              {
                const exprt &len_e = it->second.operands()[0];
                const exprt &data = it->second.operands()[1];
                if(len_e.is_constant() && data.id() == ID_array)
                {
                  mp_integer len_v;
                  if(!to_integer(to_constant_expr(len_e), len_v))
                  {
                    auto n = static_cast<std::size_t>(len_v.to_long());
                    for(std::size_t k = 0; k < n && k < data.operands().size();
                        ++k)
                      out.push_back(data.operands()[k]);
                  }
                }
              }
            }
            return out;
          };
          auto gens_it = as_array(generators).begin();
          const jsont &gen_outer = *gens_it++;
          const jsont &gen_inner = *gens_it;
          const jsont &outer_iter = json_member(gen_outer, "iter");
          const jsont &inner_iter = json_member(gen_inner, "iter");
          std::string outer_var =
            json_string(json_member(json_member(gen_outer, "target"), "id"));
          std::string inner_var =
            json_string(json_member(json_member(gen_inner, "target"), "id"));
          if(outer_var != inner_var)
          {
            // Build the outer / inner element vectors.
            std::vector<exprt> outer_vals;
            std::vector<exprt> inner_vals;
            if(is_node_type(outer_iter, "List"))
            {
              const jsont &el = json_member(outer_iter, "elts");
              if(el.is_array())
                for(const auto &e : as_array(el))
                  outer_vals.push_back(convert_expression(e));
            }
            else
            {
              outer_vals = fetch_struct_elements(outer_iter);
            }
            if(is_node_type(inner_iter, "List"))
            {
              const jsont &el = json_member(inner_iter, "elts");
              if(el.is_array())
                for(const auto &e : as_array(el))
                  inner_vals.push_back(convert_expression(e));
            }
            else
            {
              inner_vals = fetch_struct_elements(inner_iter);
            }
            if(!outer_vals.empty() && !inner_vals.empty())
            {
              irep_idt o_id{qualify_name(outer_var)};
              irep_idt i_id{qualify_name(inner_var)};
              if(symbol_table.lookup(o_id) == nullptr)
              {
                symbolt s{o_id, python_int_type(), "python"};
                s.base_name = outer_var;
                s.is_lvalue = true;
                s.is_state_var = true;
                symbol_table.add(s);
              }
              if(symbol_table.lookup(i_id) == nullptr)
              {
                symbolt s{i_id, python_int_type(), "python"};
                s.base_name = inner_var;
                s.is_lvalue = true;
                s.is_state_var = true;
                symbol_table.add(s);
              }
              exprt result = (func_name == "all") ? exprt{true_exprt{}}
                                                  : exprt{false_exprt{}};
              const jsont &outer_ifs = json_member(gen_outer, "ifs");
              const jsont &inner_ifs = json_member(gen_inner, "ifs");
              auto subst_pair =
                [&](exprt &e, const exprt &outer_v, const exprt &inner_v)
              {
                std::function<void(exprt &)> sub = [&](exprt &x)
                {
                  if(x.id() == ID_symbol)
                  {
                    auto sid = to_symbol_expr(x).get_identifier();
                    if(sid == o_id)
                      x = outer_v;
                    else if(sid == i_id)
                      x = inner_v;
                    return;
                  }
                  for(auto &op : x.operands())
                    sub(op);
                };
                sub(e);
              };
              for(const exprt &outer_val : outer_vals)
              {
                for(const exprt &inner_val : inner_vals)
                {
                  exprt elt_expr = convert_expression(elt);
                  subst_pair(elt_expr, outer_val, inner_val);
                  if(elt_expr.type() != bool_typet{})
                    elt_expr = typecast_exprt{elt_expr, bool_typet{}};
                  exprt filter = true_exprt{};
                  auto add_filter = [&](const jsont &ifs_node)
                  {
                    if(!ifs_node.is_array())
                      return;
                    for(const auto &fn : as_array(ifs_node))
                    {
                      exprt fp = convert_expression(fn);
                      subst_pair(fp, outer_val, inner_val);
                      if(fp.type() != bool_typet{})
                        fp = safe_typecast(fp, bool_typet{});
                      filter = and_exprt{filter, fp};
                    }
                  };
                  add_filter(outer_ifs);
                  add_filter(inner_ifs);
                  if(func_name == "all")
                    result =
                      and_exprt{result, or_exprt{not_exprt{filter}, elt_expr}};
                  else
                    result = or_exprt{result, and_exprt{filter, elt_expr}};
                }
              }
              return result;
            }
          }
        }
        if(generators.is_array() && !as_array(generators).empty())
        {
          const jsont &gen = *as_array(generators).begin();
          const jsont &gen_iter = json_member(gen, "iter");
          const jsont &gen_target = json_member(gen, "target");
          std::string iter_var = json_string(json_member(gen_target, "id"));
          // PLR §6.2.4: 'for x in xs if PRED(x)' filter clause —
          // skip elements that fail PRED. We support a single
          // generator with zero-or-more 'if' clauses ANDed
          // together (the common case).
          const jsont &gen_ifs = json_member(gen, "ifs");

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

                // PLR §6.2.4: apply 'if' filter clauses. The
                // generator only yields when all 'if' predicates
                // hold; for filtered-out elements all() vacuously
                // succeeds and any() doesn't contribute.
                exprt filter_pred = true_exprt{};
                if(gen_ifs.is_array())
                {
                  for(const auto &if_node : as_array(gen_ifs))
                  {
                    exprt fp = convert_expression(if_node);
                    std::function<void(exprt &)> fsubst = [&](exprt &e)
                    {
                      if(
                        e.id() == ID_symbol &&
                        to_symbol_expr(e).get_identifier() == iter_sym_id)
                        e = val;
                      else
                        for(auto &op : e.operands())
                          fsubst(op);
                    };
                    fsubst(fp);
                    if(fp.type() != bool_typet{})
                      fp = safe_typecast(fp, bool_typet{});
                    filter_pred = and_exprt{filter_pred, fp};
                  }
                }

                if(func_name == "all")
                  result = and_exprt{
                    result, or_exprt{not_exprt{filter_pred}, elt_expr}};
                else
                  result = or_exprt{result, and_exprt{filter_pred, elt_expr}};
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

                // PLR §6.2.4: generator expressions only iterate
                // over the first `length` items. Pending checks
                // emitted while converting the element expression
                // (e.g. ZeroDivisionError on `1 % x`) must be
                // guarded by `i < length`, otherwise they fire
                // for buffer slots beyond the iterable's actual
                // length (where x = 0 from zero-init), producing
                // spurious exceptions on empty lists.
                std::size_t pc_before = pending_checks.size();
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
                // Wrap each newly-added pending check in a guard:
                // only fire if `i < length`. Substitute the iter
                // symbol with the indexed element first.
                for(std::size_t pi = pc_before; pi < pending_checks.size();
                    pi++)
                {
                  codet &pc = pending_checks[pi];
                  // Substitute iter symbol → data[i] inside the
                  // check too.
                  std::function<void(exprt &)> esubst = [&](exprt &e)
                  {
                    if(
                      e.id() == ID_symbol &&
                      to_symbol_expr(e).get_identifier() == iter_sym_id)
                      e = elem;
                    else
                      for(auto &op : e.operands())
                        esubst(op);
                  };
                  for(auto &op : pc.operands())
                    esubst(op);
                  // Wrap the entire pc as: if(in_range) { pc }
                  pc = code_ifthenelset{in_range, std::move(pc)};
                }

                if(elt_expr.type() != bool_typet{})
                  elt_expr = safe_typecast(elt_expr, bool_typet{});

                // PLR §6.2.4: apply the optional 'if' filter
                // clauses. AND all filters together; element
                // contributes only when filter is true.
                exprt filter_pred = true_exprt{};
                if(gen_ifs.is_array())
                {
                  for(const auto &if_node : as_array(gen_ifs))
                  {
                    exprt fp = convert_expression(if_node);
                    // Substitute iter_var → elem in the filter
                    std::function<void(exprt &)> fsubst = [&](exprt &e)
                    {
                      if(
                        e.id() == ID_symbol &&
                        to_symbol_expr(e).get_identifier() == iter_sym_id)
                        e = elem;
                      else
                        for(auto &op : e.operands())
                          fsubst(op);
                    };
                    fsubst(fp);
                    if(fp.type() != bool_typet{})
                      fp = safe_typecast(fp, bool_typet{});
                    filter_pred = and_exprt{filter_pred, fp};
                  }
                }
                exprt eff_in_range = and_exprt{in_range, filter_pred};

                if(func_name == "all")
                  result = and_exprt{
                    result, or_exprt{not_exprt{eff_in_range}, elt_expr}};
                else
                  result = or_exprt{result, and_exprt{eff_in_range, elt_expr}};
              }
              return result;
            }

            // Variable iterable: iterate over string bytes
            // (P2.B). Unrolls up to MAX_UNROLL=16 iterations,
            // each binding the loop variable to a single-char
            // python_string struct backed by a fresh local
            // 1-byte array containing *(s.data + i).
            //
            // This produces struct literals where the byte-OR
            // fast-paths in compare ('c in const_string',
            // 'c == const_char') already fire — no
            // cprover_string_contains_func calls per iteration,
            // avoiding the SAT-loop crash that the previous
            // attempt hit on github_3036_6.
            if(!iterable.is_nil() && is_python_string_type(iterable.type()))
            {
              // Constant-string fast-path: when the iterable's
              // content is statically known (constant struct
              // literal or string_constants-tracked symbol),
              // unroll the genexp at the AST level with
              // string_constants[iter_sym_id] = "<char>" per
              // iteration. This lets the elt expression (e.g.
              // c.lower(), c.isalpha()) fold per character
              // instead of routing through the refined-string
              // solver — required for github_3036-style
              // `'a' <= c.lower() <= 'z'`.
              {
                std::optional<std::string> sv = extract_string_value(iterable);
                if(!sv.has_value() && iterable.id() == ID_symbol)
                {
                  auto sit = string_constants.find(
                    to_symbol_expr(iterable).get_identifier());
                  if(sit != string_constants.end())
                    sv = sit->second;
                }
                if(sv.has_value())
                {
                  std::string qname = qualify_name(iter_var);
                  irep_idt iter_sym_id_const{qname};
                  if(symbol_table.lookup(iter_sym_id_const) == nullptr)
                  {
                    symbolt sym{
                      iter_sym_id_const, python_string_type(), "python"};
                    sym.base_name = iter_var;
                    sym.is_lvalue = true;
                    sym.is_state_var = true;
                    symbol_table.add(sym);
                  }
                  else if(
                    symbol_table.lookup_ref(iter_sym_id_const).type !=
                    python_string_type())
                  {
                    symbol_table.get_writeable_ref(iter_sym_id_const).type =
                      python_string_type();
                  }
                  exprt result = (func_name == "all") ? exprt{true_exprt{}}
                                                      : exprt{false_exprt{}};
                  for(char ch : sv.value())
                  {
                    std::string ch_str(1, ch);
                    string_constants[iter_sym_id_const] = ch_str;
                    exprt elt_expr = convert_expression(elt);
                    if(elt_expr.type() != bool_typet{})
                      elt_expr = safe_typecast(elt_expr, bool_typet{});
                    exprt filter_pred = true_exprt{};
                    if(gen_ifs.is_array())
                    {
                      for(const auto &if_node : as_array(gen_ifs))
                      {
                        exprt fp = convert_expression(if_node);
                        if(fp.type() != bool_typet{})
                          fp = safe_typecast(fp, bool_typet{});
                        filter_pred = and_exprt{filter_pred, fp};
                      }
                    }
                    if(func_name == "all")
                      result = and_exprt{
                        result, or_exprt{not_exprt{filter_pred}, elt_expr}};
                    else
                      result =
                        or_exprt{result, and_exprt{filter_pred, elt_expr}};
                  }
                  string_constants.erase(iter_sym_id_const);
                  return result;
                }
              }

              member_exprt str_length{iterable, "length", signedbv_typet{64}};
              member_exprt data_ptr{
                iterable, "data", pointer_typet(unsignedbv_typet{8}, 64)};

              std::string qname = qualify_name(iter_var);
              irep_idt iter_sym_id{qname};
              if(symbol_table.lookup(iter_sym_id) == nullptr)
              {
                symbolt sym{iter_sym_id, python_string_type(), "python"};
                sym.base_name = iter_var;
                sym.is_lvalue = true;
                sym.is_state_var = true;
                symbol_table.add(sym);
              }
              else if(
                symbol_table.lookup_ref(iter_sym_id).type !=
                python_string_type())
              {
                symbol_table.get_writeable_ref(iter_sym_id).type =
                  python_string_type();
              }

              constexpr std::size_t MAX_STR_GENEXP_UNROLL = 16;
              exprt result = (func_name == "all") ? exprt{true_exprt{}}
                                                  : exprt{false_exprt{}};
              for(std::size_t i = 0; i < MAX_STR_GENEXP_UNROLL; i++)
              {
                exprt idx = from_integer(i, signedbv_typet{64});
                exprt in_range = binary_relation_exprt{idx, ID_lt, str_length};

                // Build the per-iteration char struct
                // {1, address_of(arr[0])} where arr[0] = byte_i.
                // The byte-OR fast-paths in compare detect this
                // shape and bypass the refined-string solver.
                dereference_exprt byte_i{plus_exprt{data_ptr, idx}};
                exprt::operandst chars;
                chars.push_back(byte_i);
                array_typet at{
                  unsignedbv_typet{8}, from_integer(1, signedbv_typet{64})};
                array_exprt arr{std::move(chars), at};
                exprt ptr = address_of_exprt{index_exprt{
                  arr,
                  from_integer(0, signedbv_typet{64}),
                  unsignedbv_typet{8}}};
                exprt len_one = from_integer(1, signedbv_typet{64});
                exprt char_struct =
                  struct_exprt{{len_one, ptr}, python_string_type()};

                std::size_t pc_before = pending_checks.size();
                exprt elt_expr = convert_expression(elt);
                std::function<void(exprt &)> subst = [&](exprt &e)
                {
                  if(
                    e.id() == ID_symbol &&
                    to_symbol_expr(e).get_identifier() == iter_sym_id)
                    e = char_struct;
                  else
                    for(auto &op : e.operands())
                      subst(op);
                };
                subst(elt_expr);
                // Guard pending checks emitted while converting
                // elt by `i < length`.
                for(std::size_t pi = pc_before; pi < pending_checks.size();
                    pi++)
                {
                  codet &pc = pending_checks[pi];
                  std::function<void(exprt &)> esubst = [&](exprt &e)
                  {
                    if(
                      e.id() == ID_symbol &&
                      to_symbol_expr(e).get_identifier() == iter_sym_id)
                      e = char_struct;
                    else
                      for(auto &op : e.operands())
                        esubst(op);
                  };
                  for(auto &op : pc.operands())
                    esubst(op);
                  pc = code_ifthenelset{in_range, std::move(pc)};
                }

                if(elt_expr.type() != bool_typet{})
                  elt_expr = safe_typecast(elt_expr, bool_typet{});

                // Apply optional 'if' filter clauses.
                exprt filter_pred = true_exprt{};
                if(gen_ifs.is_array())
                {
                  for(const auto &if_node : as_array(gen_ifs))
                  {
                    exprt fp = convert_expression(if_node);
                    std::function<void(exprt &)> fsubst = [&](exprt &e)
                    {
                      if(
                        e.id() == ID_symbol &&
                        to_symbol_expr(e).get_identifier() == iter_sym_id)
                        e = char_struct;
                      else
                        for(auto &op : e.operands())
                          fsubst(op);
                    };
                    fsubst(fp);
                    if(fp.type() != bool_typet{})
                      fp = safe_typecast(fp, bool_typet{});
                    filter_pred = and_exprt{filter_pred, fp};
                  }
                }
                exprt eff_in_range = and_exprt{in_range, filter_pred};

                if(func_name == "all")
                  result = and_exprt{
                    result, or_exprt{not_exprt{eff_in_range}, elt_expr}};
                else
                  result = or_exprt{result, and_exprt{eff_in_range, elt_expr}};
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
          // PLR §4.4: use python_truthiness so None / 0 / 0.0 /
          // empty containers / 0+0j are all correctly recognised
          // as falsy. safe_typecast(..., bool) routes through
          // unwrap_value which doesn't recognise the None
          // sentinel for INT-tagged python_value entries.
          exprt truthy = python_truthiness(elem);

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
    // PLR §3.3.5 / §4.4.4: hasattr(obj, name) — True if the
    // object has an attribute called 'name', else False.
    // For constant 'name' and a class-instance obj we can
    // resolve statically against the struct's components.
    if(func_name == "hasattr" && args.is_array() && as_array(args).size() >= 2)
    {
      auto it = as_array(args).begin();
      exprt obj = convert_expression(*it);
      ++it;
      std::string attr;
      if(is_node_type(*it, "Constant"))
      {
        const jsont &v = json_member(*it, "value");
        if(v.is_string())
          attr = json_string(v);
      }
      if(!attr.empty() && !obj.is_nil())
      {
        // Strip pointer wrap to get to the struct.
        typet ot = obj.type();
        if(ot.id() == ID_pointer)
          ot = to_pointer_type(ot).base_type();
        struct_typet st;
        if(ot.id() == ID_struct)
          st = to_struct_type(ot);
        else if(ot.id() == ID_struct_tag)
        {
          auto sym =
            symbol_table.lookup(to_struct_tag_type(ot).get_identifier());
          if(sym != nullptr && sym->type.id() == ID_struct)
            st = to_struct_type(sym->type);
        }
        if(!st.components().empty())
        {
          for(const auto &c : st.components())
          {
            if(id2string(c.get_name()) == attr)
              return true_exprt{};
          }
          // Also check for a method symbol on the class:
          // python::<Class>::<attr>.
          std::string tag = id2string(st.get_tag());
          if(tag.substr(0, 13) == "python_class_")
            tag = tag.substr(13);
          for(const std::string &prefix :
              {std::string{"python::"} + id2string(st.get_tag()) + "::" + attr,
               std::string{"python::"} + tag + "::" + attr})
          {
            if(symbol_table.lookup(irep_idt{prefix}) != nullptr)
              return true_exprt{};
          }
          return false_exprt{};
        }
      }
    }
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
      // PLR §6.10.2: if the first argument is the literal
      // 'None', isinstance(None, X) is False for every X
      // except type(None) / NoneType. We special-case the
      // common 'isinstance(None, builtin_type)' shape here so
      // None doesn't match int/str/list etc. (None is encoded
      // as a python_int sentinel, which the standard signedbv
      // dispatch below would otherwise accept as int).
      if(is_node_type(*it, "Constant") && json_member(*it, "value").is_null())
      {
        auto cls_it = it;
        ++cls_it;
        if(is_node_type(*cls_it, "Name"))
        {
          std::string nm = json_string(json_member(*cls_it, "id"));
          static const std::set<std::string> non_none_builtins = {
            "int",
            "float",
            "bool",
            "str",
            "list",
            "tuple",
            "dict",
            "set",
            "frozenset",
            "bytes",
            "bytearray",
            "complex"};
          if(non_none_builtins.count(nm) > 0)
            return false_exprt{};
        }
      }
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
              // type(None) — a Call node. Check NONE tag.
              if(
                is_node_type(elt, "Call") &&
                is_node_type(json_member(elt, "func"), "Name") &&
                json_string(json_member(json_member(elt, "func"), "id")) ==
                  "type")
              {
                const jsont &t_args = json_member(elt, "args");
                if(
                  t_args.is_array() && !as_array(t_args).empty() &&
                  is_node_type(*as_array(t_args).begin(), "Constant") &&
                  json_member(*as_array(t_args).begin(), "value").is_null())
                {
                  exprt none_match =
                    python_value_is(obj, python_type_tagt::NONE);
                  any_match = or_exprt{any_match, std::move(none_match)};
                }
                continue;
              }
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
          // isinstance(x, type(None)) — check if x is None.
          // None has two representations in our encoding:
          //   * tag NONE (when explicitly wrapped via wrap_value
          //     with a None-aware fast path)
          //   * tag INT with int_val == python_none_sentinel_int()
          //     (the legacy sentinel form used everywhere else)
          // Accept either.
          if(is_python_value_type(obj.type()))
          {
            exprt is_none_tag = python_value_is(obj, python_type_tagt::NONE);
            exprt is_int_sentinel = and_exprt{
              python_value_is(obj, python_type_tagt::INT),
              equal_exprt{
                python_value_int(obj),
                from_integer(python_none_sentinel_int(), signedbv_typet{64})}};
            return or_exprt{std::move(is_none_tag), std::move(is_int_sentinel)};
          }
          if(obj.type().id() == ID_signedbv)
          {
            // None sentinel check (legacy encoding)
            return equal_exprt{
              obj, from_integer(python_none_sentinel_int(), obj.type())};
          }
          return false_exprt{};
        }
      }

      if(!obj.is_nil() && !cls_name.empty())
      {
        // PLR §6.10.2: if the first argument is a Name bound to
        // a type object, isinstance(<type-name>, T) is true
        // ONLY when T == 'type' (which is handled below as a
        // special case). Other isinstance checks against
        // type-bound names are False — `int` is not an int
        // instance, str is not a str instance, etc. Without
        // this gate, the standard dispatch below would see x's
        // CBMC type (python_int, since type-tags are stored as
        // ints) and answer isinstance(x, int) as True.
        {
          auto first_arg_it = as_array(args).begin();
          if(is_node_type(*first_arg_it, "Name"))
          {
            std::string nm = json_string(json_member(*first_arg_it, "id"));
            std::string qn = qualify_name(nm);
            static const std::set<std::string> type_names = {
              "int",
              "float",
              "bool",
              "str",
              "list",
              "tuple",
              "dict",
              "set",
              "frozenset",
              "bytes",
              "bytearray",
              "object",
              "type",
              "Exception",
              "BaseException",
              "ValueError",
              "TypeError",
              "KeyError",
              "IndexError",
              "StopIteration",
              "AttributeError",
              "ArithmeticError",
              "ZeroDivisionError",
              "NotImplementedError",
              "RuntimeError",
              "OSError",
              "FileNotFoundError"};
            bool name_is_type = type_names.count(nm) > 0 ||
                                class_types.count(nm) > 0 ||
                                name_holds_type_binding.count(irep_idt{qn}) > 0;
            if(name_is_type && cls_name != "type")
              return false_exprt{};
          }
        }
        // PLR §6.10.2: isinstance(x, type) — checks whether x
        // is itself a type. Built-in type names (int, str,
        // etc.) and user-class names ARE types in Python; their
        // type-tag value (or class-tag value) is statically
        // known. Detect the AST-level shape: if the first
        // argument is a Name whose id is one of the type-tag
        // table entries OR a registered class name, return True.
        // For a variable `x = int`, look up x in
        // `name_holds_type_binding` to see if it was bound to
        // a type-name.
        if(cls_name == "type")
        {
          auto first_arg_it = as_array(args).begin();
          if(is_node_type(*first_arg_it, "Name"))
          {
            std::string nm = json_string(json_member(*first_arg_it, "id"));
            static const std::set<std::string> type_names = {
              "int",
              "float",
              "bool",
              "str",
              "list",
              "tuple",
              "dict",
              "set",
              "frozenset",
              "bytes",
              "bytearray",
              "object",
              "type",
              "Exception",
              "BaseException",
              "ValueError",
              "TypeError",
              "KeyError",
              "IndexError",
              "StopIteration",
              "AttributeError",
              "ArithmeticError",
              "ZeroDivisionError",
              "NotImplementedError",
              "RuntimeError",
              "OSError",
              "FileNotFoundError"};
            if(type_names.count(nm) > 0 || class_types.count(nm) > 0)
              return true_exprt{};
            std::string qn = qualify_name(nm);
            if(name_holds_type_binding.count(irep_idt{qn}) > 0)
              return true_exprt{};
            return false_exprt{};
          }
        }
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
        // Non-complex types are not complex.
        if(cls_name == "complex")
        {
          if(
            obj.type().id() == ID_signedbv || obj.type().id() == ID_integer ||
            obj.type().id() == ID_floatbv || obj.type().id() == ID_bool ||
            is_python_string_type(obj.type()) ||
            is_python_list_type(obj.type()) ||
            is_python_tuple_type(obj.type()) || is_python_dict_type(obj.type()))
            return false_exprt{};
        }

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
          // python_complex is not int/float/bool/etc.
          if(
            obj.type().id() == ID_struct &&
            to_struct_type(obj.type()).get_tag() == "python_complex")
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
              // Round-trip via strtod (exception-free). std::stod
              // throws for out-of-range denormals and would unwind
              // out of the frontend.
              const std::string rs = rv.to_ansi_c_string();
              const std::string is = iv.to_ansi_c_string();
              errno = 0;
              char *re_endp = nullptr;
              const double rd = std::strtod(rs.c_str(), &re_endp);
              char *im_endp = nullptr;
              const double id = std::strtod(is.c_str(), &im_endp);
              if(
                re_endp == rs.c_str() + rs.size() &&
                im_endp == is.c_str() + is.size())
                return double_to_floatbv(std::sqrt(rd * rd + id * id));
              // Round-trip failed; fall through to the nondet path.
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
        // PLR §6.10.2: abs() on a tagged-union value dispatches
        // on the runtime tag — return abs(int_val) when tag==INT,
        // abs(float_val) when tag==FLOAT, abs(complex) shape
        // when tag==COMPLEX, nondet otherwise. Wraps the result
        // back into python_value so 'r = abs(diff); r ==
        // expected' works downstream.
        if(is_python_value_type(arg.type()))
        {
          exprt iv = python_value_int(arg);
          exprt fv = python_value_float(arg);
          exprt abs_int = if_exprt{
            binary_relation_exprt{iv, ID_ge, safe_zero(iv.type())},
            iv,
            unary_minus_exprt{iv}};
          exprt abs_float = if_exprt{
            binary_relation_exprt{fv, ID_ge, safe_zero(fv.type())},
            fv,
            unary_minus_exprt{fv}};
          exprt int_wrapped = make_python_value(python_type_tagt::INT, abs_int);
          exprt float_wrapped =
            make_python_value(python_type_tagt::FLOAT, abs_float);
          return if_exprt{
            python_value_is(arg, python_type_tagt::FLOAT),
            float_wrapped,
            int_wrapped};
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
    if(args.is_array() && !as_array(args).empty())
    {
      // Helper: classify a type as numeric (int / float) — anything
      // else (string structs, list structs, python_value tagged
      // unions, etc.) is left to the existing fallback paths so we
      // don't regress lexicographic string comparisons or
      // tagged-union value handling.
      auto is_numeric = [](const typet &t)
      {
        return t.id() == ID_signedbv || t.id() == ID_unsignedbv ||
               t.id() == ID_integer || t.id() == ID_floatbv ||
               t.id() == ID_bool;
      };

      irep_idt op = (func_name == "min") ? ID_lt : ID_gt;

      // Single-argument list form on numeric element types: walk
      // a constant list and compute via chained if-then-else.
      if(as_array(args).size() == 1)
      {
        exprt arg = convert_expression(*as_array(args).begin());
        // Tuple-argument form: walk the struct.s components in
        // order. PLR §6.10.2 treats tuples and lists uniformly
        // for min()/max().
        if(!arg.is_nil() && is_python_tuple_type(arg.type()))
        {
          // Tuple is empty when it has no components.
          const auto &tuple_st = to_struct_type(arg.type());
          if(tuple_st.components().empty())
          {
            add_check(
              false_exprt{},
              "exception",
              std::string{"ValueError: "} + func_name +
                "() arg is an empty sequence",
              get_location(expr));
            return side_effect_expr_nondett{
              python_int_type(), get_location(expr)};
          }
          // Resolve a symbol-bound tuple to its tracked literal so
          // the constant-fold path fires for `t = (...); min(t)`.
          const exprt *tuple_val = nullptr;
          if(arg.id() == ID_struct)
            tuple_val = &arg;
          else if(arg.id() == ID_symbol)
          {
            auto it = tuple_literals.find(to_symbol_expr(arg).get_identifier());
            if(it != tuple_literals.end())
              tuple_val = &it->second;
          }
          // Constant tuple fast path: walk the operands.
          if(
            tuple_val != nullptr && tuple_val->id() == ID_struct &&
            tuple_val->operands().size() == tuple_st.components().size())
          {
            std::vector<exprt> elems;
            for(const auto &op : tuple_val->operands())
              elems.push_back(op);
            bool all_num = !elems.empty() &&
                           std::all_of(
                             elems.begin(),
                             elems.end(),
                             [&](const exprt &e)
                             {
                               const typet &t = e.type();
                               return t.id() == ID_signedbv ||
                                      t.id() == ID_unsignedbv ||
                                      t.id() == ID_floatbv ||
                                      t.id() == ID_bool || t.id() == ID_integer;
                             });
            if(all_num)
            {
              bool any_float = std::any_of(
                elems.begin(),
                elems.end(),
                [](const exprt &e) { return e.type().id() == ID_floatbv; });
              if(any_float)
              {
                for(auto &e : elems)
                  if(e.type().id() != ID_floatbv)
                    e = safe_typecast(e, double_type());
              }
              else
              {
                for(std::size_t i = 1; i < elems.size(); i++)
                  if(elems[i].type() != elems[0].type())
                    elems[i] = safe_typecast(elems[i], elems[0].type());
              }
              exprt result = elems[0];
              for(std::size_t i = 1; i < elems.size(); i++)
                result = if_exprt{
                  binary_relation_exprt{elems[i], op, result},
                  elems[i],
                  result};
              return result;
            }
          }
        }
        if(!arg.is_nil() && is_python_list_type(arg.type()))
        {
          const auto &lt = to_struct_type(arg.type());
          typet elem_type =
            to_array_type(lt.components()[1].type()).element_type();
          // PLR builtins: min()/max() on an empty sequence raises
          // ValueError. The check uses the list's runtime length
          // member, so it works for both literal-empty lists like
          // \`max([])\` and runtime-empty lists.
          {
            member_exprt list_length{arg, "length", signedbv_typet{64}};
            add_check(
              binary_relation_exprt{
                list_length, ID_gt, from_integer(0, signedbv_typet{64})},
              "exception",
              std::string{"ValueError: "} + func_name +
                "() arg is an empty sequence",
              get_location(expr));
          }
          // Constant-list fast path (numeric or value-tagged).
          const exprt *list_val = nullptr;
          if(arg.id() == ID_struct)
            list_val = &arg;
          else if(arg.id() == ID_symbol)
          {
            auto it = list_literals.find(to_symbol_expr(arg).get_identifier());
            if(it != list_literals.end())
              list_val = &it->second;
          }
          if(
            list_val != nullptr && list_val->operands().size() >= 2 &&
            list_val->operands()[0].is_constant())
          {
            mp_integer lv;
            if(!to_integer(to_constant_expr(list_val->operands()[0]), lv))
            {
              const exprt &data_arr = list_val->operands()[1];
              std::vector<exprt> elems;
              for(mp_integer i = 0; i < lv; ++i)
              {
                auto idx = i.to_ulong();
                if(idx < data_arr.operands().size())
                  elems.push_back(data_arr.operands()[idx]);
              }
              // Only handle homogeneous-numeric lists here. Mixed
              // int/float or non-numeric (string) is left to fall
              // through to the existing fallback (so we don't
              // regress lexicographic string comparison).
              bool all_int =
                !elems.empty() && std::all_of(
                                    elems.begin(),
                                    elems.end(),
                                    [&](const exprt &e)
                                    {
                                      return e.type().id() == ID_signedbv ||
                                             e.type().id() == ID_unsignedbv ||
                                             e.type().id() == ID_bool;
                                    });
              bool all_num = !elems.empty() && std::all_of(
                                                 elems.begin(),
                                                 elems.end(),
                                                 [&](const exprt &e) {
                                                   return is_numeric(e.type());
                                                 });
              if(all_int)
              {
                exprt result = elems[0];
                for(std::size_t i = 1; i < elems.size(); i++)
                  result = if_exprt{
                    binary_relation_exprt{elems[i], op, result},
                    elems[i],
                    result};
                return result;
              }
              if(all_num)
              {
                // Mixed int/float — promote everything to double.
                std::vector<exprt> promoted;
                for(auto &e : elems)
                  promoted.push_back(
                    e.type().id() == ID_floatbv
                      ? e
                      : safe_typecast(e, double_type()));
                exprt result = promoted[0];
                for(std::size_t i = 1; i < promoted.size(); i++)
                  result = if_exprt{
                    binary_relation_exprt{promoted[i], op, result},
                    promoted[i],
                    result};
                // Cast back to the declared element type when it's
                // an int (so that `max([1, 2.5, 3]) == 3` works:
                // the assertion compares to an int constant).
                if(elem_type != double_type() && is_numeric(elem_type))
                  result = safe_typecast(result, elem_type);
                return result;
              }
              // Heterogeneous list whose declared element type is
              // python_value_type (a tagged-union). Inspect the
              // tag/int_val/float_val fields of each constant
              // operand and unwrap to a concrete numeric value.
              if(
                !elems.empty() && is_python_value_type(elems[0].type()) &&
                std::all_of(
                  elems.begin(),
                  elems.end(),
                  [](const exprt &e)
                  {
                    return is_python_value_type(e.type()) &&
                           e.id() == ID_struct && e.operands().size() >= 3;
                  }))
              {
                std::vector<exprt> unwrapped;
                bool all_int_or_float = true;
                bool any_float = false;
                for(const auto &e : elems)
                {
                  // tag (int32) is field 0, int_val (int64) is
                  // field 1, float_val (double) is field 2.
                  const exprt &tag_e = e.operands()[0];
                  if(!tag_e.is_constant())
                  {
                    all_int_or_float = false;
                    break;
                  }
                  mp_integer tag_v;
                  if(to_integer(to_constant_expr(tag_e), tag_v))
                  {
                    all_int_or_float = false;
                    break;
                  }
                  if(tag_v == static_cast<int>(python_type_tagt::INT))
                    unwrapped.push_back(e.operands()[1]);
                  else if(tag_v == static_cast<int>(python_type_tagt::FLOAT))
                  {
                    unwrapped.push_back(e.operands()[2]);
                    any_float = true;
                  }
                  else
                  {
                    all_int_or_float = false;
                    break;
                  }
                }
                if(all_int_or_float && !unwrapped.empty())
                {
                  if(any_float)
                  {
                    for(auto &e : unwrapped)
                      if(e.type().id() != ID_floatbv)
                        e = safe_typecast(e, double_type());
                  }
                  else
                  {
                    for(std::size_t i = 1; i < unwrapped.size(); i++)
                      if(unwrapped[i].type() != unwrapped[0].type())
                        unwrapped[i] =
                          safe_typecast(unwrapped[i], unwrapped[0].type());
                  }
                  exprt result = unwrapped[0];
                  for(std::size_t i = 1; i < unwrapped.size(); i++)
                    result = if_exprt{
                      binary_relation_exprt{unwrapped[i], op, result},
                      unwrapped[i],
                      result};
                  return result;
                }
              }
            }
          }
          // Non-literal numeric list with a numeric element type:
          // PLR §6.10.2: emit a runtime reduction. Walk the data
          // array up to length, accumulating min/max via guarded
          // updates. This handles min(args) / max(args) where args
          // is a *args list of python_value, the typical case in
          // user-defined varargs functions.
          if(is_python_list_type(arg.type()))
          {
            const auto &list_st_mm = to_struct_type(arg.type());
            const auto &data_t_mm =
              to_array_type(list_st_mm.components()[1].type());
            const typet &elem_t_mm = data_t_mm.element_type();
            bool elem_is_value_mm = is_python_value_type(elem_t_mm);
            typet acc_type = elem_is_value_mm ? python_int_type() : elem_t_mm;
            if(is_numeric(acc_type) || elem_is_value_mm)
            {
              member_exprt llen{arg, "length", signedbv_typet{64}};
              member_exprt ldata{arg, "data", data_t_mm};
              static unsigned mm_ctr = 0;
              std::string mn = std::string{"__"} + id2string(func_name) + "_" +
                               std::to_string(mm_ctr++);
              std::string mq = qualify_name(mn);
              irep_idt mi{mq};
              if(symbol_table.lookup(mi) == nullptr)
              {
                symbolt ms{mi, acc_type, "python"};
                ms.base_name = mn;
                ms.is_lvalue = true;
                ms.is_state_var = true;
                symbol_table.add(ms);
              }
              symbol_exprt acc = symbol_table.lookup_ref(mi).symbol_expr();
              // Initialise with the first element (data[0]).
              {
                exprt e0 =
                  index_exprt{ldata, from_integer(0, signedbv_typet{64})};
                if(elem_is_value_mm)
                  e0 = unwrap_value(e0, acc_type);
                else if(e0.type() != acc_type)
                  e0 = safe_typecast(e0, acc_type);
                pending_checks.push_back(code_frontend_assignt{acc, e0});
              }
              // For i in 1..MAX, if i < length, update acc.
              for(std::size_t i = 1; i < PYTHON_MAX_LIST_LENGTH; i++)
              {
                exprt idx = from_integer(i, signedbv_typet{64});
                exprt elem = index_exprt{ldata, idx};
                if(elem_is_value_mm)
                  elem = unwrap_value(elem, acc_type);
                else if(elem.type() != acc_type)
                  elem = safe_typecast(elem, acc_type);
                exprt better = binary_relation_exprt{elem, op, acc};
                exprt in_range = binary_relation_exprt{idx, ID_lt, llen};
                pending_checks.push_back(code_ifthenelset{
                  and_exprt{in_range, better},
                  code_frontend_assignt{acc, elem}});
              }
              return std::move(acc);
            }
          }
        }
      }

      // Variadic numeric form: min(a, b, c, ...).
      if(as_array(args).size() >= 2)
      {
        std::vector<exprt> elems;
        bool all_num = true;
        for(const auto &a : as_array(args))
        {
          exprt e = convert_expression(a);
          if(e.is_nil())
            return nil_exprt{};
          if(!is_numeric(e.type()))
            all_num = false;
          elems.push_back(std::move(e));
        }
        if(all_num)
        {
          // Promote to double if any is float.
          bool any_float = std::any_of(
            elems.begin(),
            elems.end(),
            [](const exprt &e) { return e.type().id() == ID_floatbv; });
          if(any_float)
          {
            for(auto &e : elems)
              if(e.type().id() != ID_floatbv)
                e = safe_typecast(e, double_type());
          }
          else
          {
            for(std::size_t i = 1; i < elems.size(); i++)
              if(elems[i].type() != elems[0].type())
                elems[i] = safe_typecast(elems[i], elems[0].type());
          }
          exprt result = elems[0];
          for(std::size_t i = 1; i < elems.size(); i++)
            result = if_exprt{
              binary_relation_exprt{elems[i], op, result}, elems[i], result};
          return result;
        }
      }

      // Two-arg legacy fallback (preserved for non-numeric mixes
      // that previously worked, e.g. comparing python_value tagged
      // unions).
      if(as_array(args).size() == 2)
      {
        auto it = as_array(args).begin();
        exprt a = convert_expression(*it);
        ++it;
        exprt b = convert_expression(*it);
        if(!a.is_nil() && !b.is_nil())
        {
          if(a.type() != b.type())
          {
            if(a.type().id() == ID_floatbv)
              b = safe_typecast(b, a.type());
            else if(b.type().id() == ID_floatbv)
              a = safe_typecast(a, b.type());
            else
              b = safe_typecast(b, a.type());
          }
          return if_exprt{binary_relation_exprt{a, op, b}, a, b};
        }
      }
    }
    return nil_exprt{};
  }

  return std::nullopt;
}
