/// \\file
/// TypeScript to GOTO converter — call expression and method handlers

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/floatbv_expr.h>
#include <util/ieee_float.h>
#include <util/irep.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/symbol.h>

#include <goto-programs/goto_functions.h>

#include "typescript_converter.h"
#include "typescript_types.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>

exprt typescript_convertert::convert_call_expression(const jsont &node)
{
  const jsont &callee = json_member(node, "expression");
  const jsont &args = json_member(node, "arguments");

  // Handle console.assert → CBMC assertion
  if(is_kind(callee, "PropertyAccessExpression"))
  {
    std::string obj =
      json_string(json_member(json_member(callee, "expression"), "text"));
    std::string method =
      json_string(json_member(json_member(callee, "name"), "text"));

    if(obj == "console" && method == "assert")
    {
      // This is handled at the statement level
      return nil_exprt{};
    }
    // ES2024 sec-object.keys, sec-object.values
    if(obj == "Object")
    {
      exprt::operandst call_args;
      if(args.is_array())
        for(const auto &a : to_json_array(args))
          call_args.push_back(convert_expression(a));
      if((method == "keys" || method == "values") && !call_args.empty())
      {
        exprt src = call_args[0];
        if(src.id() == ID_symbol)
        {
          const symbolt *s =
            symbol_table.lookup(to_symbol_expr(src).get_identifier());
          if(s && !s->value.is_nil())
            src = s->value;
        }
        if(src.type().id() == ID_struct)
        {
          const auto &st = to_struct_type(src.type());
          exprt::operandst elts;
          for(std::size_t i = 0; i < st.components().size(); ++i)
          {
            if(method == "keys")
              elts.push_back(convert_string_literal_from_text(
                id2string(st.components()[i].get_name())));
            else if(i < src.operands().size())
              elts.push_back(src.operands()[i]);
          }
          std::size_t actual = elts.size();
          typet elem_type =
            method == "keys" ? typet{typescript_string_type()} : double_type();
          if(!elts.empty())
            elem_type = elts[0].type();
          std::size_t max_len = TYPESCRIPT_MAX_ARRAY_LENGTH;
          while(elts.size() < max_len)
          {
            if(method == "keys")
              elts.push_back(convert_string_literal_from_text(""));
            else
              elts.push_back(from_integer(0, elem_type));
          }
          array_typet arr_type{
            elem_type, from_integer(max_len, signedbv_typet{64})};
          struct_typet list_type = make_array_struct_type(arr_type);
          return struct_exprt{
            {from_integer(actual, signedbv_typet{64}),
             array_exprt{std::move(elts), arr_type}},
            list_type};
        }
      }
      return side_effect_expr_nondett{double_type(), get_location(node)};
    }
    // ES2024 sec-promise.resolve, sec-promise.reject
    if(obj == "Promise")
    {
      if(args.is_array() && !to_json_array(args).empty())
        return convert_expression(*to_json_array(args).begin());
      return side_effect_expr_nondett{double_type(), get_location(node)};
    }
    // ES2024 sec-array.isarray — returns true iff the argument has
    // array type (a struct with data+length, our typescript_array).
    if(obj == "Array" && method == "isArray")
    {
      if(args.is_array() && !to_json_array(args).empty())
      {
        exprt arg = convert_expression(*to_json_array(args).begin());
        if(arg.is_nil())
          return false_exprt{};
        // Our arrays are struct with array-typed data field.
        if(arg.type().id() == ID_struct)
        {
          const auto &st = to_struct_type(arg.type());
          // Typescript strings are also struct{length, data} — exclude.
          if(
            st.has_component("data") && st.has_component("length") &&
            !is_typescript_string_type(arg.type()))
            return true_exprt{};
        }
      }
      return false_exprt{};
    }
    // ES2024 sec-array.from
    if(obj == "Array" && method == "from")
    {
      exprt::operandst call_args;
      const jsont *cb_node = nullptr;
      if(args.is_array())
      {
        std::size_t ai = 0;
        for(const auto &a : to_json_array(args))
        {
          if(ai == 0)
            call_args.push_back(convert_expression(a));
          else if(ai == 1)
            cb_node = &a;
          ai++;
        }
      }
      if(!call_args.empty())
      {
        exprt src = call_args[0];
        if(src.id() == ID_symbol)
        {
          const symbolt *s =
            symbol_table.lookup(to_symbol_expr(src).get_identifier());
          if(s && !s->value.is_nil())
            src = s->value;
        }
        // Array.from(existingArray) or Array.from(arr, mapFn)
        if(
          src.type().id() == ID_struct &&
          to_struct_type(src.type()).get_tag() == "typescript_array")
        {
          if(cb_node == nullptr)
            return src;
          // Array.from(arr, mapFn) — apply map function to each element
          // Reuse the map handler logic: convert callback, apply to each
          mp_integer src_len{0};
          if(src.operands().size() >= 1 && src.operands()[0].is_constant())
            to_integer(to_constant_expr(src.operands()[0]), src_len);
          const exprt &data = src.operands()[1];
          // Convert callback
          static unsigned from_ctr = 0;
          std::string cb_name = "__ts_from_cb_" + std::to_string(from_ctr++);
          convert_function_declaration_with_name(*cb_node, cb_name);
          irep_idt cb_id{"typescript::" + cb_name};
          const symbolt *cb_sym = symbol_table.lookup(cb_id);
          if(cb_sym != nullptr)
          {
            typet ret_type = to_code_type(cb_sym->type).return_type();
            exprt::operandst result_elts;
            std::size_t max_len = TYPESCRIPT_MAX_ARRAY_LENGTH;
            for(std::size_t i = 0; i < max_len; ++i)
            {
              if(mp_integer(i) < src_len)
              {
                exprt elem = data.operands()[i];
                exprt idx_val = from_integer(i, double_type());
                result_elts.push_back(side_effect_expr_function_callt{
                  symbol_exprt{cb_id, cb_sym->type},
                  {elem, idx_val},
                  ret_type,
                  source_locationt{}});
              }
              else
                result_elts.push_back(from_integer(0, ret_type));
            }
            array_typet arr_type{
              ret_type, from_integer(max_len, signedbv_typet{64})};
            struct_typet list_type = make_array_struct_type(arr_type);
            return struct_exprt{
              {from_integer(src_len, signedbv_typet{64}),
               array_exprt{std::move(result_elts), arr_type}},
              list_type};
          }
          return src;
        }
      }
      return side_effect_expr_nondett{double_type(), get_location(node)};
    }
    // ES2024 sec-number.isinteger, sec-number.isnan, sec-number.isfinite
    if(obj == "Number")
    {
      exprt::operandst call_args;
      if(args.is_array())
        for(const auto &a : to_json_array(args))
          call_args.push_back(convert_expression(a));
      if(method == "isInteger" && !call_args.empty())
      {
        // x === Math.floor(x)
        if(call_args[0].is_constant() && call_args[0].type().id() == ID_floatbv)
        {
          ieee_floatt fv{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          fv.from_expr(to_constant_expr(call_args[0]));
          double d = std::stod(fv.to_ansi_c_string());
          return d == std::floor(d) ? exprt{true_exprt{}}
                                    : exprt{false_exprt{}};
        }
        return side_effect_expr_nondett{bool_typet{}, get_location(node)};
      }
      if(method == "isNaN" && !call_args.empty())
      {
        // isnan check
        return isnan_exprt{call_args[0]};
      }
      if(method == "isFinite" && !call_args.empty())
        return not_exprt{
          or_exprt{isnan_exprt{call_args[0]}, isinf_exprt{call_args[0]}}};
      return side_effect_expr_nondett{bool_typet{}, get_location(node)};
    }
  }

  // ES2024 sec-super-keyword-runtime-semantics-evaluation
  // TSH: Classes > Inheritance
  // Handle super() calls — call parent constructor
  if(is_kind(callee, "SuperKeyword") && !current_class.empty())
  {
    exprt::operandst call_args;
    if(args.is_array())
    {
      for(const auto &arg : to_json_array(args))
        call_args.push_back(convert_expression(arg));
    }
    // Use parent_class map to find the correct parent
    std::string parent_name;
    auto pit = parent_class.find(current_class);
    if(pit != parent_class.end())
      parent_name = pit->second;
    if(!parent_name.empty())
    {
      irep_idt ctor_id{"typescript::" + parent_name + "::__init__"};
      const symbolt *ctor = symbol_table.lookup(ctor_id);
      if(ctor != nullptr && ctor->type.id() == ID_code)
      {
        const auto &ctor_params = to_code_type(ctor->type).parameters();
        exprt::operandst full_args;
        std::string this_id =
          "typescript::" + current_class + "::__init__::this";
        const symbolt *this_sym = symbol_table.lookup(irep_idt{this_id});
        if(this_sym != nullptr)
        {
          exprt this_arg = this_sym->symbol_expr();
          if(!ctor_params.empty() && this_arg.type() != ctor_params[0].type())
            this_arg = typecast_exprt{this_arg, ctor_params[0].type()};
          full_args.push_back(this_arg);
        }
        for(auto &a : call_args)
          full_args.push_back(a);
        for(std::size_t i = 0; i < full_args.size() && i < ctor_params.size();
            i++)
          if(full_args[i].type() != ctor_params[i].type())
            full_args[i] = typecast_exprt{full_args[i], ctor_params[i].type()};
        return side_effect_expr_function_callt{
          symbol_exprt{ctor_id, ctor->type},
          std::move(full_args),
          to_code_type(ctor->type).return_type(),
          get_location(node)};
      }
    }
    // Fallback: search all class_types (legacy behavior)
    for(const auto &[cname, ctype] : class_types)
    {
      if(cname == current_class)
        continue;
      // Check for parent constructor (Class::Class or Class::__init__)
      irep_idt ctor_id{"typescript::" + cname + "::" + cname};
      const symbolt *ctor = symbol_table.lookup(ctor_id);
      if(ctor == nullptr)
      {
        ctor_id = irep_idt{"typescript::" + cname + "::__init__"};
        ctor = symbol_table.lookup(ctor_id);
      }
      if(ctor != nullptr && ctor->type.id() == ID_code)
      {
        // Pass this pointer + args
        const auto &ctor_params = to_code_type(ctor->type).parameters();
        exprt::operandst full_args;
        // this pointer: use current function's this parameter
        std::string this_id =
          "typescript::" + current_class + "::__init__::this";
        const symbolt *this_sym = symbol_table.lookup(irep_idt{this_id});
        if(this_sym != nullptr)
        {
          exprt this_arg = this_sym->symbol_expr();
          if(!ctor_params.empty() && this_arg.type() != ctor_params[0].type())
            this_arg = typecast_exprt{this_arg, ctor_params[0].type()};
          full_args.push_back(this_arg);
        }
        for(auto &a : call_args)
          full_args.push_back(a);
        // Typecast args
        for(std::size_t i = 0; i < full_args.size() && i < ctor_params.size();
            ++i)
        {
          if(full_args[i].type() != ctor_params[i].type())
            full_args[i] = typecast_exprt{full_args[i], ctor_params[i].type()};
        }
        return side_effect_expr_function_callt{
          symbol_exprt{ctor_id, ctor->type},
          std::move(full_args),
          to_code_type(ctor->type).return_type(),
          get_location(node)};
      }
    }
    return nil_exprt{};
  }
  // Handle method calls: obj.method(args)
  if(is_kind(callee, "PropertyAccessExpression"))
  {
    std::string obj_name =
      json_string(json_member(json_member(callee, "expression"), "text"));
    std::string method =
      json_string(json_member(json_member(callee, "name"), "text"));

    // Static method dispatch: ClassName.method(args)
    if(!obj_name.empty() && !method.empty())
    {
      irep_idt static_id{"typescript::" + obj_name + "::" + method};
      const symbolt *static_sym = symbol_table.lookup(static_id);
      if(static_sym != nullptr && static_sym->type.id() == ID_code)
      {
        exprt::operandst call_args;
        if(args.is_array())
          for(const auto &a : to_json_array(args))
            call_args.push_back(convert_expression(a));
        return side_effect_expr_function_callt{
          symbol_exprt{static_id, static_sym->type},
          std::move(call_args),
          to_code_type(static_sym->type).return_type(),
          get_location(node)};
      }
    }

    // ES2024 sec-string.prototype.indexof, sec-string.prototype.includes,
    // sec-string.prototype.substring, sec-string.prototype.replace,
    // sec-string.prototype.split, sec-string.prototype.repeat,
    // sec-string.prototype.trim, sec-string.prototype.charat,
    // sec-string.prototype.startswith, sec-string.prototype.endswith,
    // sec-string.prototype.touppercase, sec-string.prototype.tolowercase,
    // sec-string.prototype.padstart, sec-string.prototype.padend,
    // sec-string.prototype.slice, sec-string.prototype.replaceall
    // TSH: Template Literal Types
    exprt obj_expr = convert_expression(json_member(callee, "expression"));
    if(!obj_expr.is_nil() && is_typescript_string_type(obj_expr.type()))
    {
      // Try to get constant string value. Track whether we HAVE one
      // separately from whether it's empty, so that e.g. "".concat("x")
      // still dispatches to the concat handler.
      std::string sv;
      bool sv_known = false;
      {
        std::string raw = extract_string_value(obj_expr);
        if(!raw.empty())
        {
          sv = raw.substr(2);
          sv_known = true;
        }
      }
      // Get method arguments as constant strings/numbers
      std::vector<std::string> str_args;
      std::vector<int> num_args;
      if(args.is_array())
      {
        for(const auto &a : to_json_array(args))
        {
          exprt av = convert_expression(a);
          // Try to extract string constant
          std::string sv = extract_string_value(av);
          if(!sv.empty())
          {
            str_args.push_back(sv.substr(2));
            continue;
          }
          // Try number (including unary minus)
          if(av.is_constant() && av.type().id() == ID_floatbv)
          {
            ieee_floatt fv{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            fv.from_expr(to_constant_expr(av));
            num_args.push_back(
              static_cast<int>(std::stod(fv.to_ansi_c_string())));
          }
          else if(
            av.id() == ID_unary_minus && !av.operands().empty() &&
            av.operands()[0].is_constant() &&
            av.operands()[0].type().id() == ID_floatbv)
          {
            ieee_floatt fv{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            fv.from_expr(to_constant_expr(av.operands()[0]));
            num_args.push_back(
              -static_cast<int>(std::stod(fv.to_ansi_c_string())));
          }
          str_args.push_back("");
        }
      }
      if(sv_known)
      {
        if(method == "indexOf" && !str_args.empty())
        {
          // ES2024 §22.1.3.9: String.prototype.indexOf(searchString, fromIndex)
          // fromIndex defaults to 0 and is clamped to [0, length].
          // Find the first string arg (skipping empty placeholders for
          // numeric args).
          std::string needle;
          for(const auto &s : str_args)
            if(!s.empty() || needle.empty())
              needle = s;
          // Actually: the first arg is the string; if we have num_args[0]
          // it's the fromIndex. Use a simpler heuristic — str_args[0] is
          // the needle (it's the first arg; if numeric, no indexOf).
          needle = str_args[0];
          size_t from = 0;
          if(!num_args.empty())
          {
            int fi = num_args[0];
            if(fi < 0)
              fi = 0;
            if(fi > static_cast<int>(sv.size()))
              fi = sv.size();
            from = static_cast<size_t>(fi);
          }
          auto pos = sv.find(needle, from);
          int result = (pos == std::string::npos) ? -1 : static_cast<int>(pos);
          uint64_t bits;
          double dv = static_cast<double>(result);
          std::memcpy(&bits, &dv, sizeof(bits));
          return constant_exprt{
            integer2bvrep(mp_integer{std::to_string(bits).c_str()}, 64),
            double_type()};
        }
        if(method == "includes" && !str_args.empty())
          return sv.find(str_args[0]) != std::string::npos
                   ? exprt{true_exprt{}}
                   : exprt{false_exprt{}};
        if(method == "substring" && num_args.size() >= 2)
        {
          // ES2024 §22.1.3.21: clamp both indices to [0, length], then
          // swap if start > end.
          int start = num_args[0], end = num_args[1];
          int len = static_cast<int>(sv.size());
          if(start < 0)
            start = 0;
          if(end < 0)
            end = 0;
          if(start > len)
            start = len;
          if(end > len)
            end = len;
          if(start > end)
            std::swap(start, end);
          return convert_string_literal_from_text(
            sv.substr(start, end - start));
        }
        if(method == "substring" && num_args.size() >= 1)
        {
          int start = num_args[0];
          int len = static_cast<int>(sv.size());
          if(start < 0)
            start = 0;
          if(start > len)
            start = len;
          return convert_string_literal_from_text(sv.substr(start));
        }
        if(method == "toUpperCase")
        {
          std::string upper = sv;
          for(auto &c : upper)
            c = std::toupper(c);
          return convert_string_literal_from_text(upper);
        }
        if(method == "toLowerCase")
        {
          std::string lower = sv;
          for(auto &c : lower)
            c = std::tolower(c);
          return convert_string_literal_from_text(lower);
        }
        if(method == "trim")
        {
          auto s = sv;
          s.erase(0, s.find_first_not_of(" \t\n\r"));
          s.erase(s.find_last_not_of(" \t\n\r") + 1);
          return convert_string_literal_from_text(s);
        }
        // ES2024 §22.1.3.30 / §22.1.3.31: trimStart, trimEnd.
        if(method == "trimStart")
        {
          auto s = sv;
          s.erase(0, s.find_first_not_of(" \t\n\r"));
          return convert_string_literal_from_text(s);
        }
        if(method == "trimEnd")
        {
          auto s = sv;
          auto pos = s.find_last_not_of(" \t\n\r");
          if(pos != std::string::npos)
            s.erase(pos + 1);
          else
            s.clear();
          return convert_string_literal_from_text(s);
        }
        if(method == "charAt" && !num_args.empty())
        {
          int idx = num_args[0];
          if(idx >= 0 && idx < static_cast<int>(sv.size()))
            return convert_string_literal_from_text(std::string(1, sv[idx]));
          return convert_string_literal_from_text("");
        }
        if(method == "startsWith" && !str_args.empty())
        {
          // ES2024 §22.1.3.23: startsWith(searchString, position)
          std::string needle = str_args[0];
          size_t pos = 0;
          if(!num_args.empty())
          {
            int p = num_args[0];
            if(p < 0)
              p = 0;
            if(p > static_cast<int>(sv.size()))
              p = sv.size();
            pos = static_cast<size_t>(p);
          }
          if(pos + needle.size() > sv.size())
            return false_exprt{};
          return sv.substr(pos, needle.size()) == needle ? exprt{true_exprt{}}
                                                         : exprt{false_exprt{}};
        }
        if(method == "endsWith" && !str_args.empty())
        {
          // ES2024 §22.1.3.7: endsWith(searchString, endPosition)
          std::string needle = str_args[0];
          size_t end_pos = sv.size();
          if(!num_args.empty())
          {
            int p = num_args[0];
            if(p < 0)
              p = 0;
            if(p > static_cast<int>(sv.size()))
              p = sv.size();
            end_pos = static_cast<size_t>(p);
          }
          if(needle.size() > end_pos)
            return false_exprt{};
          return sv.substr(end_pos - needle.size(), needle.size()) == needle
                   ? exprt{true_exprt{}}
                   : exprt{false_exprt{}};
        }
        if(method == "replace" && str_args.size() >= 2)
        {
          auto pos = sv.find(str_args[0]);
          if(pos != std::string::npos)
          {
            std::string r = sv;
            r.replace(pos, str_args[0].size(), str_args[1]);
            return convert_string_literal_from_text(r);
          }
          return convert_string_literal_from_text(sv);
        }
        if(method == "slice")
        {
          int start_idx = 0, end_idx = static_cast<int>(sv.size());
          if(!num_args.empty())
            start_idx = num_args[0];
          if(num_args.size() >= 2)
            end_idx = num_args[1];
          // Handle negative indices
          if(start_idx < 0)
            start_idx = std::max(0, static_cast<int>(sv.size()) + start_idx);
          if(end_idx < 0)
            end_idx = std::max(0, static_cast<int>(sv.size()) + end_idx);
          if(end_idx > static_cast<int>(sv.size()))
            end_idx = sv.size();
          if(start_idx >= end_idx)
            return convert_string_literal_from_text("");
          return convert_string_literal_from_text(
            sv.substr(start_idx, end_idx - start_idx));
        }
        if(method == "padStart" && !num_args.empty())
        {
          int target_len = num_args[0];
          // ES2024 §22.1.3.17: if current length >= target, return
          // the string unchanged.
          if(static_cast<int>(sv.size()) >= target_len)
            return convert_string_literal_from_text(sv);
          std::string pad = " ";
          for(const auto &s : str_args)
            if(!s.empty())
              pad = s;
          // ES2024 StringPad: build a filler string by repeating `pad`
          // enough times to cover (target_len - sv.size()) chars, then
          // TRUNCATE to that size and prepend to sv. This matches the
          // spec's "slice to required length" step and correctly
          // handles multi-char pad strings.
          int need = target_len - static_cast<int>(sv.size());
          std::string filler;
          filler.reserve(need);
          int max_iters = 10000;
          while(static_cast<int>(filler.size()) < need && max_iters-- > 0)
            filler += pad;
          filler = filler.substr(0, need);
          return convert_string_literal_from_text(filler + sv);
        }
        if(method == "padEnd" && !num_args.empty())
        {
          int target_len = num_args[0];
          if(static_cast<int>(sv.size()) >= target_len)
            return convert_string_literal_from_text(sv);
          std::string pad = " ";
          for(const auto &s : str_args)
            if(!s.empty())
              pad = s;
          int need = target_len - static_cast<int>(sv.size());
          std::string filler;
          filler.reserve(need);
          int max_iters = 10000;
          while(static_cast<int>(filler.size()) < need && max_iters-- > 0)
            filler += pad;
          filler = filler.substr(0, need);
          return convert_string_literal_from_text(sv + filler);
        }
        if(method == "repeat" && !num_args.empty())
        {
          int count = num_args[0];
          // Safety: cap count to avoid runaway allocations.
          if(count < 0)
            count = 0;
          if(count > 10000)
            count = 10000;
          std::string result;
          for(int i = 0; i < count; ++i)
            result += sv;
          return convert_string_literal_from_text(result);
        }
        // ES2024 §22.1.3.5: String.prototype.concat
        if(method == "concat" && !str_args.empty())
        {
          std::string result = sv;
          for(const auto &s : str_args)
            result += s;
          return convert_string_literal_from_text(result);
        }
        // ES2024 §22.1.3.11: String.prototype.lastIndexOf
        if(method == "lastIndexOf" && !str_args.empty())
        {
          std::string needle;
          for(const auto &s : str_args)
            if(!s.empty() || needle.empty())
              needle = s;
          needle = str_args[0];
          auto pos = sv.rfind(needle);
          int result = (pos == std::string::npos) ? -1 : static_cast<int>(pos);
          uint64_t bits;
          double dv = static_cast<double>(result);
          std::memcpy(&bits, &dv, sizeof(bits));
          return constant_exprt{
            integer2bvrep(mp_integer{std::to_string(bits).c_str()}, 64),
            double_type()};
        }
        if(method == "replaceAll" && str_args.size() >= 2)
        {
          std::string result = sv;
          std::string from = str_args[0];
          std::string to_str = str_args[1];
          std::size_t pos = 0;
          while((pos = result.find(from, pos)) != std::string::npos)
          {
            result.replace(pos, from.size(), to_str);
            pos += to_str.size();
          }
          return convert_string_literal_from_text(result);
        }
        if(method == "split" && !str_args.empty())
        {
          std::string delim = str_args[0];
          std::vector<std::string> parts;
          if(delim.empty())
          {
            // split("") splits into individual characters
            for(char c : sv)
              parts.push_back(std::string(1, c));
          }
          else
          {
            std::string tmp = sv;
            while(true)
            {
              auto pos = tmp.find(delim);
              if(pos == std::string::npos)
              {
                parts.push_back(tmp);
                break;
              }
              parts.push_back(tmp.substr(0, pos));
              tmp = tmp.substr(pos + delim.size());
            }
          }
          // Build array of strings
          exprt::operandst elts;
          typet elem_type = typescript_string_type();
          for(const auto &p : parts)
            elts.push_back(convert_string_literal_from_text(p));
          std::size_t actual = elts.size();
          std::size_t max_len = TYPESCRIPT_MAX_ARRAY_LENGTH;
          while(elts.size() < max_len)
            elts.push_back(convert_string_literal_from_text(""));
          array_typet arr_type{
            elem_type, from_integer(max_len, signedbv_typet{64})};
          struct_typet list_type = make_array_struct_type(arr_type);
          return struct_exprt{
            {from_integer(actual, signedbv_typet{64}),
             array_exprt{std::move(elts), arr_type}},
            list_type};
        }
      }
      // ES2024 sec-string.prototype.indexof
      // indexOf on non-constant strings with constant search target:
      // scan data array for matching substring
      if(method == "indexOf" && args.is_array() && !to_json_array(args).empty())
      {
        exprt arg = convert_expression(*to_json_array(args).begin());
        std::string search_sv = extract_string_value(arg);
        if(!search_sv.empty())
        {
          std::string needle = search_sv.substr(2);
          struct_typet str_type = typescript_string_type();
          const auto &data_type =
            to_array_type(str_type.components()[1].type());
          exprt data = member_exprt{obj_expr, "data", data_type};
          exprt len_e = member_exprt{obj_expr, "length", signedbv_typet{32}};
          // Build: for each possible position p, check if data[p..p+n-1] == needle
          // Return the first matching position, or -1.
          // Limit scan depth to avoid huge formulas.
          std::size_t nlen = needle.size();
          std::size_t max_scan =
            std::min<std::size_t>(TYPESCRIPT_MAX_STRING_LENGTH, 32);
          exprt result = from_integer(-1, signedbv_typet{64});
          for(int p = static_cast<int>(max_scan) - 1 - static_cast<int>(nlen);
              p >= 0;
              p--)
          {
            if(p + static_cast<int>(nlen) > TYPESCRIPT_MAX_STRING_LENGTH)
              continue;
            // Check all characters match
            exprt matches = true_exprt{};
            for(std::size_t j = 0; j < nlen; ++j)
            {
              exprt idx = from_integer(p + j, signedbv_typet{64});
              exprt char_at = index_exprt{data, idx};
              exprt expected = from_integer(
                static_cast<unsigned char>(needle[j]), unsignedbv_typet{16});
              matches = and_exprt{matches, equal_exprt{char_at, expected}};
            }
            // Also check p + nlen <= len
            exprt in_bounds = binary_relation_exprt{
              from_integer(p + nlen, signedbv_typet{32}), ID_le, len_e};
            exprt cond = and_exprt{matches, in_bounds};
            result =
              if_exprt{cond, from_integer(p, signedbv_typet{64}), result};
          }
          // Cast to double (typescript number)
          return typecast_exprt{result, double_type()};
        }
      }
      // ES2024 sec-string.prototype.substring
      // substring on non-constant strings — copy data from start..end
      if(
        method == "substring" && args.is_array() &&
        to_json_array(args).size() >= 1)
      {
        auto it = to_json_array(args).begin();
        exprt start = convert_expression(*it);
        ++it;
        exprt end_arg;
        if(it != to_json_array(args).end())
          end_arg = convert_expression(*it);
        struct_typet str_type = typescript_string_type();
        const auto &data_type = to_array_type(str_type.components()[1].type());
        exprt data = member_exprt{obj_expr, "data", data_type};
        exprt len_e = member_exprt{obj_expr, "length", signedbv_typet{32}};
        if(start.type() != signedbv_typet{32})
          start = typecast_exprt{start, signedbv_typet{32}};
        exprt end_e;
        if(end_arg.is_nil())
          end_e = len_e;
        else
        {
          if(end_arg.type() != signedbv_typet{32})
            end_arg = typecast_exprt{end_arg, signedbv_typet{32}};
          end_e = end_arg;
        }
        // Build result: for each position i, if i < (end - start), copy data[start+i], else 0
        exprt::operandst chars;
        for(std::size_t i = 0; i < TYPESCRIPT_MAX_STRING_LENGTH; ++i)
        {
          exprt offset = plus_exprt{start, from_integer(i, signedbv_typet{32})};
          exprt in_range = binary_relation_exprt{offset, ID_lt, end_e};
          exprt offset_64 = typecast_exprt{offset, signedbv_typet{64}};
          exprt char_at = index_exprt{data, offset_64};
          exprt zero = from_integer(0, unsignedbv_typet{16});
          chars.push_back(if_exprt{in_range, char_at, zero});
        }
        exprt new_len = minus_exprt{end_e, start};
        return struct_exprt{
          {new_len, array_exprt{std::move(chars), data_type}}, str_type};
      }
      // ES2024 sec-string.prototype.charat
      // charAt with non-constant index: access data[idx], build 1-char string
      if(method == "charAt" && args.is_array() && !to_json_array(args).empty())
      {
        exprt idx_expr = convert_expression(*to_json_array(args).begin());
        if(idx_expr.type() != signedbv_typet{64})
          idx_expr = typecast_exprt{idx_expr, signedbv_typet{64}};
        // Access obj_expr.data[idx]
        struct_typet str_type = typescript_string_type();
        const auto &data_type = to_array_type(str_type.components()[1].type());
        exprt data = member_exprt{obj_expr, "data", data_type};
        exprt char_val = index_exprt{data, idx_expr};
        // Build single-char string
        exprt::operandst chars;
        chars.push_back(char_val);
        while(chars.size() < TYPESCRIPT_MAX_STRING_LENGTH)
          chars.push_back(from_integer(0, unsignedbv_typet{16}));
        return struct_exprt{
          {from_integer(1, signedbv_typet{32}),
           array_exprt{std::move(chars), data_type}},
          str_type};
      }
      // ES2024 sec-string.prototype.split
      // split("") on non-constant string: build array of single-char strings
      if(method == "split" && args.is_array() && !to_json_array(args).empty())
      {
        exprt sep_expr = convert_expression(*to_json_array(args).begin());
        std::string sep_sv = extract_string_value(sep_expr);
        if(sep_sv == "S:") // empty separator
        {
          struct_typet str_type = typescript_string_type();
          const auto &str_data_type =
            to_array_type(str_type.components()[1].type());
          exprt src_data = member_exprt{obj_expr, "data", str_data_type};
          exprt src_len = member_exprt{obj_expr, "length", signedbv_typet{32}};
          // Build array of single-char strings
          exprt::operandst elts;
          std::size_t max_len = TYPESCRIPT_MAX_ARRAY_LENGTH;
          for(std::size_t i = 0; i < max_len; ++i)
          {
            exprt char_val =
              index_exprt{src_data, from_integer(i, signedbv_typet{64})};
            exprt::operandst chars;
            chars.push_back(char_val);
            while(chars.size() < TYPESCRIPT_MAX_STRING_LENGTH)
              chars.push_back(from_integer(0, unsignedbv_typet{16}));
            elts.push_back(struct_exprt{
              {from_integer(1, signedbv_typet{32}),
               array_exprt{std::move(chars), str_data_type}},
              str_type});
          }
          array_typet arr_type{
            str_type, from_integer(max_len, signedbv_typet{64})};
          struct_typet list_type = make_array_struct_type(arr_type);
          return struct_exprt{
            {typecast_exprt{src_len, signedbv_typet{64}},
             array_exprt{std::move(elts), arr_type}},
            list_type};
        }
      }
      // Nondet fallback for non-constant strings
      return side_effect_expr_nondett{double_type(), get_location(node)};
    }
    // ES2024 sec-array.prototype.map
    // Array.map: create new array by applying callback to each element
    if(
      !obj_expr.is_nil() && obj_expr.type().id() == ID_struct &&
      to_struct_type(obj_expr.type()).get_tag() == "typescript_array" &&
      method == "map" && args.is_array() && !to_json_array(args).empty())
    {
      const jsont &callback = *to_json_array(args).begin();
      // Detect filter().map() chain: if obj_expr is unresolvable symbol,
      // check if the callee's expression is a filter call on a constant array
      const jsont &inner_expr = json_member(callee, "expression");
      if(obj_expr.id() == ID_symbol && is_kind(inner_expr, "CallExpression"))
      {
        const jsont &inner_callee = json_member(inner_expr, "expression");
        if(is_kind(inner_callee, "PropertyAccessExpression"))
        {
          std::string inner_method =
            json_string(json_member(json_member(inner_callee, "name"), "text"));
          if(inner_method == "filter")
          {
            // Get the source array
            exprt src_arr =
              convert_expression(json_member(inner_callee, "expression"));
            if(src_arr.id() == ID_symbol)
            {
              const symbolt *ss =
                symbol_table.lookup(to_symbol_expr(src_arr).get_identifier());
              if(ss && !ss->value.is_nil())
                src_arr = ss->value;
            }
            if(src_arr.id() == ID_struct && src_arr.operands().size() >= 2)
            {
              // Get filter predicate and map function
              const jsont &filter_args = json_member(inner_expr, "arguments");
              if(filter_args.is_array() && !to_json_array(filter_args).empty())
              {
                const jsont &filter_cb = *to_json_array(filter_args).begin();
                // Convert both callbacks
                static unsigned fm_ctr = 0;
                std::string fcb_name =
                  "__ts_fm_filter_" + std::to_string(fm_ctr);
                std::string mcb_name =
                  "__ts_fm_map_" + std::to_string(fm_ctr++);
                convert_function_declaration_with_name(filter_cb, fcb_name);
                convert_function_declaration_with_name(callback, mcb_name);
                irep_idt fcb_id{"typescript::" + fcb_name};
                irep_idt mcb_id{"typescript::" + mcb_name};
                const symbolt *fcb = symbol_table.lookup(fcb_id);
                const symbolt *mcb = symbol_table.lookup(mcb_id);
                if(fcb && mcb)
                {
                  mp_integer src_len{0};
                  if(src_arr.operands()[0].is_constant())
                    to_integer(
                      to_constant_expr(src_arr.operands()[0]), src_len);
                  const exprt &data = src_arr.operands()[1];
                  typet map_ret = to_code_type(mcb->type).return_type();
                  // For each element: call filter, if true call map
                  exprt::operandst result_elts;
                  for(mp_integer i = 0; i < src_len; ++i)
                  {
                    auto ci = i.to_ulong();
                    if(ci >= data.operands().size())
                      break;
                    exprt elem = data.operands()[ci];
                    // Call filter predicate
                    std::string fr_name = "__ts_fm_fr_" +
                                          std::to_string(fm_ctr) + "_" +
                                          std::to_string(ci);
                    irep_idt fr_id{"typescript::" + fr_name};
                    if(symbol_table.lookup(fr_id) == nullptr)
                    {
                      symbolt frs{fr_id, bool_typet{}, "typescript"};
                      frs.base_name = fr_name;
                      frs.is_lvalue = true;
                      frs.is_state_var = true;
                      symbol_table.add(frs);
                    }
                    pending_stmts.push_back(code_frontend_assignt{
                      symbol_exprt{fr_id, bool_typet{}},
                      side_effect_expr_function_callt{
                        symbol_exprt{fcb_id, fcb->type},
                        {elem},
                        bool_typet{},
                        source_locationt{}}});
                    // Call map function (result used conditionally)
                    std::string mr_name = "__ts_fm_mr_" +
                                          std::to_string(fm_ctr) + "_" +
                                          std::to_string(ci);
                    irep_idt mr_id{"typescript::" + mr_name};
                    if(symbol_table.lookup(mr_id) == nullptr)
                    {
                      symbolt mrs{mr_id, map_ret, "typescript"};
                      mrs.base_name = mr_name;
                      mrs.is_lvalue = true;
                      mrs.is_state_var = true;
                      symbol_table.add(mrs);
                    }
                    pending_stmts.push_back(code_frontend_assignt{
                      symbol_exprt{mr_id, map_ret},
                      side_effect_expr_function_callt{
                        symbol_exprt{mcb_id, mcb->type},
                        {elem, from_integer(ci, double_type())},
                        map_ret,
                        source_locationt{}}});
                    result_elts.push_back(symbol_exprt{mr_id, map_ret});
                  }
                  // Build result: only include elements where filter=true
                  // Use a write-index approach
                  std::string res_name =
                    "__ts_fm_res_" + std::to_string(fm_ctr);
                  std::size_t max_len = TYPESCRIPT_MAX_ARRAY_LENGTH;
                  array_typet arr_type{
                    map_ret, from_integer(max_len, signedbv_typet{64})};
                  struct_typet list_type = make_array_struct_type(arr_type);
                  irep_idt res_id{"typescript::" + res_name};
                  if(symbol_table.lookup(res_id) == nullptr)
                  {
                    symbolt rs{res_id, list_type, "typescript"};
                    rs.base_name = res_name;
                    rs.is_lvalue = true;
                    rs.is_state_var = true;
                    symbol_table.add(rs);
                  }
                  std::string wi_name = "__ts_fm_wi_" + std::to_string(fm_ctr);
                  irep_idt wi_id{"typescript::" + wi_name};
                  if(symbol_table.lookup(wi_id) == nullptr)
                  {
                    symbolt ws{wi_id, signedbv_typet{64}, "typescript"};
                    ws.base_name = wi_name;
                    ws.is_lvalue = true;
                    ws.is_state_var = true;
                    symbol_table.add(ws);
                  }
                  symbol_exprt wi{wi_id, signedbv_typet{64}};
                  pending_stmts.push_back(code_frontend_assignt{
                    wi, from_integer(0, signedbv_typet{64})});
                  for(std::size_t ci = 0; ci < result_elts.size(); ++ci)
                  {
                    std::string fr_name2 = "__ts_fm_fr_" +
                                           std::to_string(fm_ctr) + "_" +
                                           std::to_string(ci);
                    irep_idt fr_id2{"typescript::" + fr_name2};
                    pending_stmts.push_back(code_ifthenelset{
                      symbol_exprt{fr_id2, bool_typet{}},
                      code_blockt{
                        {code_frontend_assignt{
                           index_exprt{
                             member_exprt{
                               symbol_exprt{res_id, list_type},
                               "data",
                               arr_type},
                             wi},
                           result_elts[ci]},
                         code_frontend_assignt{
                           wi,
                           plus_exprt{
                             wi, from_integer(1, signedbv_typet{64})}}}}});
                  }
                  pending_stmts.push_back(code_frontend_assignt{
                    member_exprt{
                      symbol_exprt{res_id, list_type},
                      "length",
                      signedbv_typet{64}},
                    wi});
                  return symbol_exprt{res_id, list_type};
                }
              }
            }
          }
        }
      }
      // For chained calls, resolve symbol chains (symbol → symbol → struct)
      if(obj_expr.id() == ID_symbol)
      {
        exprt resolved = obj_expr;
        for(int depth = 0; depth < 3 && resolved.id() == ID_symbol; ++depth)
        {
          const symbolt *s =
            symbol_table.lookup(to_symbol_expr(resolved).get_identifier());
          if(s && !s->value.is_nil())
            resolved = s->value;
          else
            break;
        }
        if(resolved.id() == ID_struct)
          obj_expr = resolved;
      }
      // Check if callback is a function pointer (Identifier)
      std::string cb_kind = json_string(json_member(callback, "_kind"));
      if(cb_kind == "Identifier")
      {
        // The callback is a variable holding a function pointer
        exprt cb_expr = convert_expression(callback);
        if(
          cb_expr.id() == ID_symbol &&
          (cb_expr.type().id() == ID_code || cb_expr.type().id() == ID_pointer))
        {
          // Use indirect call through function pointer
          exprt src2 = obj_expr;
          if(src2.id() == ID_symbol)
          {
            const symbolt *s2 =
              symbol_table.lookup(to_symbol_expr(src2).get_identifier());
            if(s2 && !s2->value.is_nil())
              src2 = s2->value;
          }
          if(src2.id() == ID_struct && src2.operands().size() >= 2)
          {
            mp_integer len2{0};
            if(src2.operands()[0].is_constant())
              to_integer(to_constant_expr(src2.operands()[0]), len2);
            const exprt &data2 = src2.operands()[1];
            typet elem_type2 = double_type();
            exprt::operandst result_elts2;
            typet callee_type = cb_expr.type();
            if(callee_type.id() == ID_pointer)
              callee_type = to_pointer_type(callee_type).base_type();
            typet ret_type2 = callee_type.id() == ID_code
                                ? to_code_type(callee_type).return_type()
                                : double_type();
            exprt callee2 = cb_expr.type().id() == ID_pointer
                              ? exprt{dereference_exprt{cb_expr}}
                              : cb_expr;
            for(mp_integer i = 0; i < len2; ++i)
            {
              auto idx = i.to_ulong();
              if(idx >= data2.operands().size())
                break;
              elem_type2 = ret_type2;
              std::string tmp = "__ts_map_fp_" + std::to_string(idx);
              std::string tmp_q = "typescript::" + tmp;
              irep_idt tmp_id{tmp_q};
              if(symbol_table.lookup(tmp_id) == nullptr)
              {
                symbolt ts{tmp_id, ret_type2, "typescript"};
                ts.base_name = tmp;
                ts.is_lvalue = true;
                ts.is_state_var = true;
                symbol_table.add(ts);
              }
              pending_stmts.push_back(code_frontend_assignt{
                symbol_exprt{tmp_id, ret_type2},
                side_effect_expr_function_callt{
                  callee2,
                  {data2.operands()[idx]},
                  ret_type2,
                  source_locationt{}}});
              result_elts2.push_back(symbol_exprt{tmp_id, ret_type2});
            }
            std::size_t actual2 = result_elts2.size();
            std::size_t max_len = TYPESCRIPT_MAX_ARRAY_LENGTH;
            while(result_elts2.size() < max_len)
              result_elts2.push_back(from_integer(0, elem_type2));
            array_typet arr_type2{
              elem_type2, from_integer(max_len, signedbv_typet{64})};
            struct_typet list_type2;
            list_type2.components().push_back(
              struct_typet::componentt{"length", signedbv_typet{64}});
            list_type2.components().push_back(
              struct_typet::componentt{"data", arr_type2});
            list_type2.set_tag("typescript_array");
            return struct_exprt{
              {from_integer(actual2, signedbv_typet{64}),
               array_exprt{std::move(result_elts2), arr_type2}},
              list_type2};
          }
        }
      }
      // Resolve source array to its value
      exprt src = obj_expr;
      if(src.id() == ID_symbol)
      {
        const symbolt *s =
          symbol_table.lookup(to_symbol_expr(src).get_identifier());
        if(s && !s->value.is_nil())
          src = s->value;
      }
      if(src.id() == ID_struct && src.operands().size() >= 2)
      {
        mp_integer len{0};
        if(src.operands()[0].is_constant())
          to_integer(to_constant_expr(src.operands()[0]), len);
        const exprt &data = src.operands()[1];

        // Convert callback as a named function
        static unsigned map_ctr = 0;
        std::string cb_name = "__ts_map_cb_" + std::to_string(map_ctr++);
        convert_function_declaration_with_name(callback, cb_name);
        irep_idt cb_id{"typescript::" + cb_name};

        // Build result array by calling callback for each element
        exprt::operandst result_elts;
        typet elem_type = double_type();
        for(mp_integer i = 0; i < len; ++i)
        {
          auto idx = i.to_ulong();
          if(idx < data.operands().size())
          {
            // Create call: cb(element)
            // Pass element and index to callback
            exprt::operandst cb_args;
            cb_args.push_back(data.operands()[idx]);
            // Add index if callback accepts 2+ params
            const symbolt &cb_sym_ref = symbol_table.lookup_ref(cb_id);
            if(
              cb_sym_ref.type.id() == ID_code &&
              to_code_type(cb_sym_ref.type).parameters().size() >= 2)
            {
              ieee_floatt idx_fv{
                ieee_float_spect::double_precision(),
                ieee_floatt::rounding_modet::ROUND_TO_EVEN};
              idx_fv.from_double(static_cast<double>(idx));
              cb_args.push_back(idx_fv.to_expr());
            }
            side_effect_expr_function_callt call{
              symbol_exprt{cb_id, cb_sym_ref.type},
              std::move(cb_args),
              double_type(),
              source_locationt{}};
            elem_type = call.type();
            // Store call result in temp
            std::string tmp = "__ts_map_tmp_" + std::to_string(map_ctr) + "_" +
                              std::to_string(idx);
            std::string tmp_q = "typescript::" + tmp;
            irep_idt tmp_id{tmp_q};
            symbolt tmp_sym{tmp_id, elem_type, "typescript"};
            tmp_sym.base_name = tmp;
            tmp_sym.is_lvalue = true;
            tmp_sym.is_state_var = true;
            if(symbol_table.lookup(tmp_id) == nullptr)
              symbol_table.add(tmp_sym);
            pending_stmts.push_back(
              code_frontend_assignt{symbol_exprt{tmp_id, elem_type}, call});
            result_elts.push_back(symbol_exprt{tmp_id, elem_type});
          }
        }
        std::size_t actual_len = result_elts.size();
        std::size_t max_len = TYPESCRIPT_MAX_ARRAY_LENGTH;
        while(result_elts.size() < max_len)
          result_elts.push_back(from_integer(0, elem_type));
        array_typet arr_type{
          elem_type, from_integer(max_len, signedbv_typet{64})};
        struct_typet list_type = make_array_struct_type(arr_type);
        return struct_exprt{
          {from_integer(actual_len, signedbv_typet{64}),
           array_exprt{std::move(result_elts), arr_type}},
          list_type};
      }
    }
    // ES2024 sec-array.prototype.filter
    // Array.filter: create new array with elements passing predicate
    if(
      !obj_expr.is_nil() && obj_expr.type().id() == ID_struct &&
      to_struct_type(obj_expr.type()).get_tag() == "typescript_array" &&
      method == "filter" && args.is_array() && !to_json_array(args).empty())
    {
      const jsont &callback = *to_json_array(args).begin();
      // Check if callback is a function pointer
      std::string filter_cb_kind = json_string(json_member(callback, "_kind"));
      if(filter_cb_kind == "Identifier")
      {
        exprt cb_expr = convert_expression(callback);
        if(
          cb_expr.id() == ID_symbol &&
          (cb_expr.type().id() == ID_code || cb_expr.type().id() == ID_pointer))
        {
          // Indirect filter with function pointer
          exprt src_fp = obj_expr;
          if(src_fp.id() == ID_symbol)
          {
            const symbolt *s =
              symbol_table.lookup(to_symbol_expr(src_fp).get_identifier());
            if(s && !s->value.is_nil())
              src_fp = s->value;
          }
          if(src_fp.id() == ID_struct && src_fp.operands().size() >= 2)
          {
            mp_integer len_fp{0};
            if(src_fp.operands()[0].is_constant())
              to_integer(to_constant_expr(src_fp.operands()[0]), len_fp);
            const exprt &data_fp = src_fp.operands()[1];
            typet elem_type_fp = double_type();
            if(!data_fp.operands().empty())
              elem_type_fp = data_fp.operands()[0].type();
            exprt callee_fp = cb_expr;
            // Write index and result
            static unsigned fp_filter_ctr = 0;
            unsigned ffc = fp_filter_ctr++;
            std::string wi_n = "__ts_fpf_wi_" + std::to_string(ffc);
            irep_idt wi_id{"typescript::" + wi_n};
            {
              symbolt ws{wi_id, signedbv_typet{64}, "typescript"};
              ws.base_name = wi_n;
              ws.is_lvalue = true;
              ws.is_state_var = true;
              if(symbol_table.lookup(wi_id) == nullptr)
                symbol_table.add(ws);
            }
            std::string res_n = "__ts_fpf_res_" + std::to_string(ffc);
            irep_idt res_id{"typescript::" + res_n};
            std::size_t max_len = TYPESCRIPT_MAX_ARRAY_LENGTH;
            array_typet arr_type_fp{
              elem_type_fp, from_integer(max_len, signedbv_typet{64})};
            struct_typet list_type_fp;
            list_type_fp.components().push_back(
              struct_typet::componentt{"length", signedbv_typet{64}});
            list_type_fp.components().push_back(
              struct_typet::componentt{"data", arr_type_fp});
            list_type_fp.set_tag("typescript_array");
            {
              symbolt rs{res_id, list_type_fp, "typescript"};
              rs.base_name = res_n;
              rs.is_lvalue = true;
              rs.is_state_var = true;
              if(symbol_table.lookup(res_id) == nullptr)
                symbol_table.add(rs);
            }
            symbol_exprt wi_sym{wi_id, signedbv_typet{64}};
            pending_stmts.push_back(code_frontend_assignt{
              wi_sym, from_integer(0, signedbv_typet{64})});
            for(mp_integer i = 0; i < len_fp; ++i)
            {
              auto idx = i.to_ulong();
              if(idx >= data_fp.operands().size())
                break;
              std::string p_n =
                "__ts_fpf_p_" + std::to_string(ffc) + "_" + std::to_string(idx);
              irep_idt p_id{"typescript::" + p_n};
              {
                symbolt ps{p_id, bool_typet{}, "typescript"};
                ps.base_name = p_n;
                ps.is_lvalue = true;
                ps.is_state_var = true;
                if(symbol_table.lookup(p_id) == nullptr)
                  symbol_table.add(ps);
              }
              pending_stmts.push_back(code_frontend_assignt{
                symbol_exprt{p_id, bool_typet{}},
                side_effect_expr_function_callt{
                  callee_fp,
                  {data_fp.operands()[idx]},
                  bool_typet{},
                  source_locationt{}}});
              pending_stmts.push_back(code_ifthenelset{
                symbol_exprt{p_id, bool_typet{}},
                code_blockt{
                  {code_frontend_assignt{
                     index_exprt{
                       member_exprt{
                         symbol_exprt{res_id, list_type_fp},
                         "data",
                         arr_type_fp},
                       wi_sym},
                     data_fp.operands()[idx]},
                   code_frontend_assignt{
                     wi_sym,
                     plus_exprt{
                       wi_sym, from_integer(1, signedbv_typet{64})}}}}});
            }
            pending_stmts.push_back(code_frontend_assignt{
              member_exprt{
                symbol_exprt{res_id, list_type_fp},
                "length",
                signedbv_typet{64}},
              wi_sym});
            return symbol_exprt{res_id, list_type_fp};
          }
        }
      }
      // Try constant evaluation: for constant arrays, evaluate predicate
      // at conversion time and return struct_exprt directly
      {
        exprt const_src = obj_expr;
        if(const_src.id() == ID_symbol)
        {
          const symbolt *cs =
            symbol_table.lookup(to_symbol_expr(const_src).get_identifier());
          if(cs && !cs->value.is_nil())
            const_src = cs->value;
        }
        if(const_src.id() == ID_struct && const_src.operands().size() >= 2)
        {
          mp_integer clen{0};
          if(const_src.operands()[0].is_constant())
            to_integer(to_constant_expr(const_src.operands()[0]), clen);
          const exprt &cdata = const_src.operands()[1];
          // Convert callback and try to evaluate for each element
          static unsigned cfilter_ctr = 0;
          std::string ccb_name =
            "__ts_cfilter_cb_" + std::to_string(cfilter_ctr++);
          convert_function_declaration_with_name(callback, ccb_name);
          irep_idt ccb_id{"typescript::" + ccb_name};
          const symbolt *ccb_sym = symbol_table.lookup(ccb_id);
          if(ccb_sym != nullptr && ccb_sym->type.id() == ID_code)
          {
            // Call predicate for each element and collect results
            typet elem_type = double_type();
            if(!cdata.operands().empty())
              elem_type = cdata.operands()[0].type();
            exprt::operandst result_elts;
            // Use pending_stmts to call predicate, then check results
            std::vector<std::pair<irep_idt, std::size_t>> pred_results;
            for(mp_integer i = 0; i < clen; ++i)
            {
              auto ci = i.to_ulong();
              if(ci >= cdata.operands().size())
                break;
              std::string pn = "__ts_cfp_" + std::to_string(cfilter_ctr) + "_" +
                               std::to_string(ci);
              irep_idt pid{"typescript::" + pn};
              {
                symbolt ps{pid, bool_typet{}, "typescript"};
                ps.base_name = pn;
                ps.is_lvalue = true;
                ps.is_state_var = true;
                if(symbol_table.lookup(pid) == nullptr)
                  symbol_table.add(ps);
              }
              pending_stmts.push_back(code_frontend_assignt{
                symbol_exprt{pid, bool_typet{}},
                side_effect_expr_function_callt{
                  symbol_exprt{ccb_id, ccb_sym->type},
                  {cdata.operands()[ci], from_integer(ci, double_type())},
                  bool_typet{},
                  source_locationt{}}});
              pred_results.emplace_back(pid, ci);
            }
            // Build result: for each element where pred is true, include it
            // Use if_exprt chain to build the result array
            // Actually, use the same pending_stmts approach but store result
            std::string res_n = "__ts_cfres_" + std::to_string(cfilter_ctr);
            irep_idt res_id{"typescript::" + res_n};
            std::size_t max_len = TYPESCRIPT_MAX_ARRAY_LENGTH;
            array_typet arr_type{
              elem_type, from_integer(max_len, signedbv_typet{64})};
            struct_typet list_type = make_array_struct_type(arr_type);
            {
              symbolt rs{res_id, list_type, "typescript"};
              rs.base_name = res_n;
              rs.is_lvalue = true;
              rs.is_state_var = true;
              if(symbol_table.lookup(res_id) == nullptr)
                symbol_table.add(rs);
            }
            std::string wi_n = "__ts_cfwi_" + std::to_string(cfilter_ctr);
            irep_idt wi_id{"typescript::" + wi_n};
            {
              symbolt ws{wi_id, signedbv_typet{64}, "typescript"};
              ws.base_name = wi_n;
              ws.is_lvalue = true;
              ws.is_state_var = true;
              if(symbol_table.lookup(wi_id) == nullptr)
                symbol_table.add(ws);
            }
            symbol_exprt wi_sym{wi_id, signedbv_typet{64}};
            pending_stmts.push_back(code_frontend_assignt{
              wi_sym, from_integer(0, signedbv_typet{64})});
            for(const auto &[pid2, ci2] : pred_results)
            {
              pending_stmts.push_back(code_ifthenelset{
                symbol_exprt{pid2, bool_typet{}},
                code_blockt{
                  {code_frontend_assignt{
                     index_exprt{
                       member_exprt{
                         symbol_exprt{res_id, list_type}, "data", arr_type},
                       wi_sym},
                     cdata.operands()[ci2]},
                   code_frontend_assignt{
                     wi_sym,
                     plus_exprt{
                       wi_sym, from_integer(1, signedbv_typet{64})}}}}});
            }
            pending_stmts.push_back(code_frontend_assignt{
              member_exprt{
                symbol_exprt{res_id, list_type}, "length", signedbv_typet{64}},
              wi_sym});
            // Store the result value so chained calls can resolve it
            // Can't store runtime value, but return the symbol
            return symbol_exprt{res_id, list_type};
          }
        }
      }
      exprt src = obj_expr;
      if(src.id() == ID_symbol)
      {
        const symbolt *s =
          symbol_table.lookup(to_symbol_expr(src).get_identifier());
        if(s && !s->value.is_nil())
          src = s->value;
      }
      if(src.id() == ID_struct && src.operands().size() >= 2)
      {
        mp_integer len{0};
        if(src.operands()[0].is_constant())
          to_integer(to_constant_expr(src.operands()[0]), len);
        const exprt &data = src.operands()[1];

        static unsigned filter_ctr = 0;
        unsigned fc = filter_ctr++;
        std::string cb_name = "__ts_filter_cb_" + std::to_string(fc);
        convert_function_declaration_with_name(callback, cb_name);
        irep_idt cb_id{"typescript::" + cb_name};

        typet elem_type = double_type();
        if(len > 0 && !data.operands().empty())
          elem_type = data.operands()[0].type();

        // Create result array symbol
        std::string res_name = "__ts_filter_res_" + std::to_string(fc);
        std::string res_q = "typescript::" + res_name;
        irep_idt res_id{res_q};
        std::size_t max_len = TYPESCRIPT_MAX_ARRAY_LENGTH;
        array_typet arr_type{
          elem_type, from_integer(max_len, signedbv_typet{64})};
        struct_typet list_type = make_array_struct_type(arr_type);
        {
          symbolt rs{res_id, list_type, "typescript"};
          rs.base_name = res_name;
          rs.is_lvalue = true;
          rs.is_state_var = true;
          if(symbol_table.lookup(res_id) == nullptr)
            symbol_table.add(rs);
        }

        // Create write index
        std::string wi_name = "__ts_filter_wi_" + std::to_string(fc);
        std::string wi_q = "typescript::" + wi_name;
        irep_idt wi_id{wi_q};
        {
          symbolt ws{wi_id, signedbv_typet{64}, "typescript"};
          ws.base_name = wi_name;
          ws.is_lvalue = true;
          ws.is_state_var = true;
          if(symbol_table.lookup(wi_id) == nullptr)
            symbol_table.add(ws);
        }
        symbol_exprt wi_sym{wi_id, signedbv_typet{64}};
        pending_stmts.push_back(
          code_frontend_assignt{wi_sym, from_integer(0, signedbv_typet{64})});

        // For each source element: call predicate, if true copy to result[wi++]
        for(mp_integer i = 0; i < len; ++i)
        {
          auto idx = i.to_ulong();
          if(idx >= data.operands().size())
            break;

          // Call predicate
          std::string pred_name =
            "__ts_fp_" + std::to_string(fc) + "_" + std::to_string(idx);
          std::string pred_q = "typescript::" + pred_name;
          irep_idt pred_id{pred_q};
          {
            symbolt ps{pred_id, bool_typet{}, "typescript"};
            ps.base_name = pred_name;
            ps.is_lvalue = true;
            ps.is_state_var = true;
            if(symbol_table.lookup(pred_id) == nullptr)
              symbol_table.add(ps);
          }
          side_effect_expr_function_callt pred_call{
            symbol_exprt{cb_id, symbol_table.lookup_ref(cb_id).type},
            {data.operands()[idx], from_integer(idx, double_type())},
            bool_typet{},
            source_locationt{}};
          pending_stmts.push_back(code_frontend_assignt{
            symbol_exprt{pred_id, bool_typet{}}, pred_call});

          // if(pred) { result.data[wi] = elem; wi++; }
          symbol_exprt res_sym{res_id, list_type};
          code_ifthenelset cond{
            symbol_exprt{pred_id, bool_typet{}},
            code_blockt{
              {code_frontend_assignt{
                 index_exprt{member_exprt{res_sym, "data", arr_type}, wi_sym},
                 data.operands()[idx]},
               code_frontend_assignt{
                 wi_sym,
                 plus_exprt{wi_sym, from_integer(1, signedbv_typet{64})}}}}};
          pending_stmts.push_back(std::move(cond));
        }
        // Set result.length = wi
        pending_stmts.push_back(code_frontend_assignt{
          member_exprt{
            symbol_exprt{res_id, list_type}, "length", signedbv_typet{64}},
          wi_sym});

        return symbol_exprt{res_id, list_type};
      }
    }
    // ES2024 sec-array.prototype.reduce
    // Array.reduce: fold array with accumulator
    if(
      !obj_expr.is_nil() && obj_expr.type().id() == ID_struct &&
      to_struct_type(obj_expr.type()).get_tag() == "typescript_array" &&
      method == "reduce" && args.is_array() && to_json_array(args).size() >= 2)
    {
      const auto &arg_arr = to_json_array(args);
      auto it = arg_arr.begin();
      const jsont &callback = *it;
      ++it;
      exprt init_val = convert_expression(*it);

      // String reduce: compute at conversion time for constant arrays
      if(is_typescript_string_type(init_val.type()))
      {
        exprt src_sr = obj_expr;
        if(src_sr.id() == ID_symbol)
        {
          const symbolt *s =
            symbol_table.lookup(to_symbol_expr(src_sr).get_identifier());
          if(s && !s->value.is_nil())
            src_sr = s->value;
        }
        if(src_sr.id() == ID_struct && src_sr.operands().size() >= 2)
        {
          mp_integer len_sr{0};
          if(src_sr.operands()[0].is_constant())
            to_integer(to_constant_expr(src_sr.operands()[0]), len_sr);
          const exprt &data_sr = src_sr.operands()[1];
          // Extract init value
          std::string acc = extract_string_value(init_val);
          acc = acc.empty() ? "" : acc.substr(2);
          bool all_const = true;
          // Extract callback body to find the concatenation pattern
          // For now, assume callback is (a, b) => a + sep + b
          // Extract separator from callback body if possible
          std::string sep = "";
          const jsont &cb_body = json_member(callback, "body");
          if(cb_body.is_object())
          {
            // Try to find string literals in the body
            const jsont &cb_left = json_member(cb_body, "left");
            if(
              cb_left.is_object() &&
              json_string(json_member(cb_left, "_kind")) == "BinaryExpression")
            {
              const jsont &cb_right_inner = json_member(cb_left, "right");
              if(
                cb_right_inner.is_object() &&
                json_string(json_member(cb_right_inner, "_kind")) ==
                  "StringLiteral")
                sep = json_string(json_member(cb_right_inner, "text"));
            }
          }
          // Compute result
          for(mp_integer i = 0; i < len_sr; ++i)
          {
            auto idx = i.to_ulong();
            if(idx >= data_sr.operands().size())
              break;
            std::string elem = extract_string_value(data_sr.operands()[idx]);
            if(elem.empty())
            {
              all_const = false;
              break;
            }
            elem = elem.substr(2);
            acc = acc + sep + elem;
          }
          if(all_const)
            return convert_string_literal_from_text(acc);
        }
      }

      exprt src = obj_expr;
      if(src.id() == ID_symbol)
      {
        const symbolt *s =
          symbol_table.lookup(to_symbol_expr(src).get_identifier());
        if(s && !s->value.is_nil())
          src = s->value;
      }
      if(src.id() == ID_struct && src.operands().size() >= 2)
      {
        mp_integer len{0};
        if(src.operands()[0].is_constant())
          to_integer(to_constant_expr(src.operands()[0]), len);
        const exprt &data = src.operands()[1];

        static unsigned reduce_ctr = 0;
        std::string cb_name = "__ts_reduce_cb_" + std::to_string(reduce_ctr++);
        convert_function_declaration_with_name(callback, cb_name);
        irep_idt cb_id{"typescript::" + cb_name};

        // Create accumulator variable
        std::string acc_name = "__ts_reduce_acc_" + std::to_string(reduce_ctr);
        std::string acc_q = "typescript::" + acc_name;
        irep_idt acc_id{acc_q};
        typet acc_type = init_val.type();
        {
          symbolt as{acc_id, acc_type, "typescript"};
          as.base_name = acc_name;
          as.is_lvalue = true;
          as.is_state_var = true;
          if(symbol_table.lookup(acc_id) == nullptr)
            symbol_table.add(as);
        }
        pending_stmts.push_back(
          code_frontend_assignt{symbol_exprt{acc_id, acc_type}, init_val});

        for(mp_integer i = 0; i < len; ++i)
        {
          auto idx = i.to_ulong();
          if(idx >= data.operands().size())
            break;
          side_effect_expr_function_callt call{
            symbol_exprt{cb_id, symbol_table.lookup_ref(cb_id).type},
            {symbol_exprt{acc_id, acc_type},
             data.operands()[idx],
             from_integer(idx, double_type())},
            acc_type,
            source_locationt{}};
          pending_stmts.push_back(
            code_frontend_assignt{symbol_exprt{acc_id, acc_type}, call});
        }
        return symbol_exprt{acc_id, acc_type};
      }
    }
    // ES2024 sec-array.prototype.slice
    // Array.slice: extract subarray
    if(
      !obj_expr.is_nil() && obj_expr.type().id() == ID_struct &&
      to_struct_type(obj_expr.type()).get_tag() == "typescript_array" &&
      method == "slice" && args.is_array())
    {
      exprt src = obj_expr;
      if(src.id() == ID_symbol)
      {
        const symbolt *s =
          symbol_table.lookup(to_symbol_expr(src).get_identifier());
        if(s && !s->value.is_nil())
          src = s->value;
      }
      if(src.id() == ID_struct && src.operands().size() >= 2)
      {
        mp_integer src_len{0};
        if(src.operands()[0].is_constant())
          to_integer(to_constant_expr(src.operands()[0]), src_len);
        const exprt &data = src.operands()[1];

        // Get start and end indices
        int start_idx = 0, end_idx = src_len.to_long();
        const auto &arg_arr = to_json_array(args);
        auto ait = arg_arr.begin();
        if(ait != arg_arr.end())
        {
          exprt sv = convert_expression(*ait);
          if(sv.is_constant() && sv.type().id() == ID_floatbv)
          {
            ieee_floatt fv{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            fv.from_expr(to_constant_expr(sv));
            start_idx = static_cast<int>(std::stod(fv.to_ansi_c_string()));
          }
          ++ait;
        }
        if(ait != arg_arr.end())
        {
          exprt ev = convert_expression(*ait);
          if(ev.is_constant() && ev.type().id() == ID_floatbv)
          {
            ieee_floatt fv{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            fv.from_expr(to_constant_expr(ev));
            end_idx = static_cast<int>(std::stod(fv.to_ansi_c_string()));
          }
        }
        if(start_idx < 0)
          start_idx = 0;
        if(end_idx > src_len.to_long())
          end_idx = src_len.to_long();

        typet elem_type = double_type();
        if(!data.operands().empty())
          elem_type = data.operands()[0].type();

        exprt::operandst result_elts;
        for(int i = start_idx; i < end_idx; ++i)
        {
          if(static_cast<std::size_t>(i) < data.operands().size())
            result_elts.push_back(data.operands()[i]);
        }
        std::size_t actual = result_elts.size();
        std::size_t max_len = TYPESCRIPT_MAX_ARRAY_LENGTH;
        while(result_elts.size() < max_len)
          result_elts.push_back(from_integer(0, elem_type));
        array_typet arr_type{
          elem_type, from_integer(max_len, signedbv_typet{64})};
        struct_typet list_type = make_array_struct_type(arr_type);
        return struct_exprt{
          {from_integer(actual, signedbv_typet{64}),
           array_exprt{std::move(result_elts), arr_type}},
          list_type};
      }
    }
    // ES2024 sec-array.prototype.find
    // Array.find: return first element matching predicate
    if(
      !obj_expr.is_nil() && obj_expr.type().id() == ID_struct &&
      to_struct_type(obj_expr.type()).get_tag() == "typescript_array" &&
      method == "find" && args.is_array() && !to_json_array(args).empty())
    {
      const jsont &callback = *to_json_array(args).begin();
      exprt src = obj_expr;
      if(src.id() == ID_symbol)
      {
        const symbolt *s =
          symbol_table.lookup(to_symbol_expr(src).get_identifier());
        if(s && !s->value.is_nil())
          src = s->value;
      }
      if(src.id() == ID_struct && src.operands().size() >= 2)
      {
        mp_integer len{0};
        if(src.operands()[0].is_constant())
          to_integer(to_constant_expr(src.operands()[0]), len);
        const exprt &data = src.operands()[1];
        static unsigned find_ctr = 0;
        unsigned fc = find_ctr++;
        std::string cb_name = "__ts_find_cb_" + std::to_string(fc);
        convert_function_declaration_with_name(callback, cb_name);
        irep_idt cb_id{"typescript::" + cb_name};
        typet elem_type = double_type();
        if(!data.operands().empty())
          elem_type = data.operands()[0].type();
        // Result variable
        std::string res_name = "__ts_find_res_" + std::to_string(fc);
        irep_idt res_id{"typescript::" + res_name};
        {
          symbolt rs{res_id, elem_type, "typescript"};
          rs.base_name = res_name;
          rs.is_lvalue = true;
          rs.is_state_var = true;
          if(symbol_table.lookup(res_id) == nullptr)
            symbol_table.add(rs);
        }
        // Found flag
        std::string flag_name = "__ts_find_flag_" + std::to_string(fc);
        irep_idt flag_id{"typescript::" + flag_name};
        {
          symbolt fs{flag_id, bool_typet{}, "typescript"};
          fs.base_name = flag_name;
          fs.is_lvalue = true;
          fs.is_state_var = true;
          if(symbol_table.lookup(flag_id) == nullptr)
            symbol_table.add(fs);
        }
        pending_stmts.push_back(code_frontend_assignt{
          symbol_exprt{flag_id, bool_typet{}}, false_exprt{}});
        // Initialize result to 0 (returned if nothing found)
        pending_stmts.push_back(code_frontend_assignt{
          symbol_exprt{res_id, elem_type}, from_integer(0, elem_type)});
        for(mp_integer i = 0; i < len; ++i)
        {
          auto idx = i.to_ulong();
          if(idx >= data.operands().size())
            break;
          std::string p =
            "__ts_find_p_" + std::to_string(fc) + "_" + std::to_string(idx);
          irep_idt pid{"typescript::" + p};
          {
            symbolt ps{pid, bool_typet{}, "typescript"};
            ps.base_name = p;
            ps.is_lvalue = true;
            ps.is_state_var = true;
            if(symbol_table.lookup(pid) == nullptr)
              symbol_table.add(ps);
          }
          pending_stmts.push_back(code_frontend_assignt{
            symbol_exprt{pid, bool_typet{}},
            side_effect_expr_function_callt{
              symbol_exprt{cb_id, symbol_table.lookup_ref(cb_id).type},
              {data.operands()[idx]},
              bool_typet{},
              source_locationt{}}});
          // if(!found && pred) { result = elem; found = true; }
          pending_stmts.push_back(code_ifthenelset{
            and_exprt{
              not_exprt{symbol_exprt{flag_id, bool_typet{}}},
              symbol_exprt{pid, bool_typet{}}},
            code_blockt{
              {code_frontend_assignt{
                 symbol_exprt{res_id, elem_type}, data.operands()[idx]},
               code_frontend_assignt{
                 symbol_exprt{flag_id, bool_typet{}}, true_exprt{}}}}});
        }
        return symbol_exprt{res_id, elem_type};
      }
    }
    // ES2024 sec-array.prototype.findindex
    // Array.findIndex: find index of first matching element
    if(
      !obj_expr.is_nil() && obj_expr.type().id() == ID_struct &&
      to_struct_type(obj_expr.type()).get_tag() == "typescript_array" &&
      method == "findIndex" && args.is_array() && !to_json_array(args).empty())
    {
      const jsont &callback = *to_json_array(args).begin();
      exprt src = obj_expr;
      if(src.id() == ID_symbol)
      {
        const symbolt *s =
          symbol_table.lookup(to_symbol_expr(src).get_identifier());
        if(s && !s->value.is_nil())
          src = s->value;
      }
      if(src.id() == ID_struct && src.operands().size() >= 2)
      {
        mp_integer len{0};
        if(src.operands()[0].is_constant())
          to_integer(to_constant_expr(src.operands()[0]), len);
        const exprt &data = src.operands()[1];
        static unsigned fi_ctr = 0;
        unsigned fc = fi_ctr++;
        std::string cb_name = "__ts_fi_cb_" + std::to_string(fc);
        convert_function_declaration_with_name(callback, cb_name);
        irep_idt cb_id{"typescript::" + cb_name};
        // Result and found flag
        std::string res_n = "__ts_fi_res_" + std::to_string(fc);
        irep_idt res_id{"typescript::" + res_n};
        {
          symbolt rs{res_id, double_type(), "typescript"};
          rs.base_name = res_n;
          rs.is_lvalue = true;
          rs.is_state_var = true;
          if(symbol_table.lookup(res_id) == nullptr)
            symbol_table.add(rs);
        }
        std::string flag_n = "__ts_fi_flag_" + std::to_string(fc);
        irep_idt flag_id{"typescript::" + flag_n};
        {
          symbolt fs{flag_id, bool_typet{}, "typescript"};
          fs.base_name = flag_n;
          fs.is_lvalue = true;
          fs.is_state_var = true;
          if(symbol_table.lookup(flag_id) == nullptr)
            symbol_table.add(fs);
        }
        // Initialize: result = -1, found = false
        pending_stmts.push_back(code_frontend_assignt{
          symbol_exprt{res_id, double_type()},
          from_integer(0, double_type())}); // will set to -1 below
        pending_stmts.push_back(code_frontend_assignt{
          symbol_exprt{flag_id, bool_typet{}}, false_exprt{}});
        // Set result to -1
        {
          ieee_floatt neg1{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          neg1.from_double(-1.0);
          pending_stmts.push_back(code_frontend_assignt{
            symbol_exprt{res_id, double_type()}, neg1.to_expr()});
        }
        for(mp_integer i = 0; i < len; ++i)
        {
          auto idx = i.to_ulong();
          if(idx >= data.operands().size())
            break;
          std::string p_n =
            "__ts_fi_p_" + std::to_string(fc) + "_" + std::to_string(idx);
          irep_idt p_id{"typescript::" + p_n};
          {
            symbolt ps{p_id, bool_typet{}, "typescript"};
            ps.base_name = p_n;
            ps.is_lvalue = true;
            ps.is_state_var = true;
            if(symbol_table.lookup(p_id) == nullptr)
              symbol_table.add(ps);
          }
          pending_stmts.push_back(code_frontend_assignt{
            symbol_exprt{p_id, bool_typet{}},
            side_effect_expr_function_callt{
              symbol_exprt{cb_id, symbol_table.lookup_ref(cb_id).type},
              {data.operands()[idx]},
              bool_typet{},
              source_locationt{}}});
          ieee_floatt idx_val{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          idx_val.from_double(static_cast<double>(idx));
          pending_stmts.push_back(code_ifthenelset{
            and_exprt{
              not_exprt{symbol_exprt{flag_id, bool_typet{}}},
              symbol_exprt{p_id, bool_typet{}}},
            code_blockt{
              {code_frontend_assignt{
                 symbol_exprt{res_id, double_type()}, idx_val.to_expr()},
               code_frontend_assignt{
                 symbol_exprt{flag_id, bool_typet{}}, true_exprt{}}}}});
        }
        return symbol_exprt{res_id, double_type()};
      }
    }
    // ES2024 sec-array.prototype.includes
    // Array.includes: check if element exists
    if(
      !obj_expr.is_nil() && obj_expr.type().id() == ID_struct &&
      to_struct_type(obj_expr.type()).get_tag() == "typescript_array" &&
      method == "includes" && args.is_array() && !to_json_array(args).empty())
    {
      exprt target = convert_expression(*to_json_array(args).begin());
      exprt src = obj_expr;
      if(src.id() == ID_symbol)
      {
        const symbolt *s =
          symbol_table.lookup(to_symbol_expr(src).get_identifier());
        if(s && !s->value.is_nil())
          src = s->value;
      }
      if(src.id() == ID_struct && src.operands().size() >= 2)
      {
        mp_integer len{0};
        if(src.operands()[0].is_constant())
          to_integer(to_constant_expr(src.operands()[0]), len);
        const exprt &data = src.operands()[1];
        // OR together equality checks for each element
        exprt result = false_exprt{};
        for(mp_integer i = 0; i < len; ++i)
        {
          auto idx = i.to_ulong();
          if(idx >= data.operands().size())
            break;
          exprt eq =
            target.type().id() == ID_floatbv
              ? exprt{ieee_float_equal_exprt{data.operands()[idx], target}}
              : exprt{equal_exprt{data.operands()[idx], target}};
          result = or_exprt{result, eq};
        }
        return result;
      }
    }
    // ES2024 sec-array.prototype.join
    // Array.join: concatenate elements with separator
    if(
      !obj_expr.is_nil() && obj_expr.type().id() == ID_struct &&
      to_struct_type(obj_expr.type()).get_tag() == "typescript_array" &&
      method == "join" && args.is_array())
    {
      std::string sep = ",";
      if(!to_json_array(args).empty())
      {
        exprt sep_expr = convert_expression(*to_json_array(args).begin());
        std::string sv = extract_string_value(sep_expr);
        if(!sv.empty())
          sep = sv.substr(2);
      }
      exprt src = obj_expr;
      if(src.id() == ID_symbol)
      {
        const symbolt *s =
          symbol_table.lookup(to_symbol_expr(src).get_identifier());
        if(s && !s->value.is_nil())
          src = s->value;
      }
      if(src.id() == ID_struct && src.operands().size() >= 2)
      {
        mp_integer len{0};
        if(src.operands()[0].is_constant())
          to_integer(to_constant_expr(src.operands()[0]), len);
        const exprt &data = src.operands()[1];
        std::string result;
        for(mp_integer i = 0; i < len; ++i)
        {
          if(i > 0)
            result += sep;
          auto idx = i.to_ulong();
          if(idx < data.operands().size())
          {
            const exprt &elem = data.operands()[idx];
            // Try string element first
            std::string elem_sv = extract_string_value(elem);
            if(!elem_sv.empty())
            {
              result += elem_sv.substr(2);
            }
            else if(elem.is_constant() && elem.type().id() == ID_floatbv)
            {
              ieee_floatt fv{
                ieee_float_spect::double_precision(),
                ieee_floatt::rounding_modet::ROUND_TO_EVEN};
              fv.from_expr(to_constant_expr(elem));
              double d = std::stod(fv.to_ansi_c_string());
              if(d == std::floor(d) && std::abs(d) < 1e15)
                result += std::to_string(static_cast<long long>(d));
              else
                result += fv.to_ansi_c_string();
            }
          }
        }
        if(!result.empty())
          return convert_string_literal_from_text(result);
      }
      // Fallback for non-constant string arrays: build result by copying
      // data[0] from each element (for join("") on single-char strings)
      if(src.id() == ID_struct && src.operands().size() >= 2 && sep.empty())
      {
        mp_integer len{0};
        if(src.operands()[0].is_constant())
          to_integer(to_constant_expr(src.operands()[0]), len);
        const exprt &data = src.operands()[1];
        if(
          len > 0 && !data.operands().empty() &&
          is_typescript_string_type(data.operands()[0].type()))
        {
          struct_typet str_type = typescript_string_type();
          const auto &char_arr_type =
            to_array_type(str_type.components()[1].type());
          // Build result: copy data[0] from each element into result
          exprt::operandst result_chars;
          for(mp_integer i = 0; i < len && i < TYPESCRIPT_MAX_STRING_LENGTH;
              ++i)
          {
            auto idx = i.to_ulong();
            if(idx < data.operands().size())
            {
              const exprt &elem = data.operands()[idx];
              // elem is a string struct — get its data[0]
              exprt elem_data = member_exprt{elem, "data", char_arr_type};
              result_chars.push_back(
                index_exprt{elem_data, from_integer(0, signedbv_typet{64})});
            }
          }
          while(result_chars.size() < TYPESCRIPT_MAX_STRING_LENGTH)
            result_chars.push_back(from_integer(0, unsignedbv_typet{16}));
          return struct_exprt{
            {from_integer(len, signedbv_typet{32}),
             array_exprt{std::move(result_chars), char_arr_type}},
            str_type};
        }
      }
      return side_effect_expr_nondett{
        typescript_string_type(), get_location(node)};
    }
    // ES2024 sec-array.prototype.sort
    // We support sort on constant arrays with a comparator returning
    // a numeric difference. The algorithm:
    //   1. Extract the constant array elements.
    //   2. Bubble-sort using CBMC-side numeric comparison (for common
    //      ascending/descending comparators we evaluate at conversion
    //      time; for arbitrary comparators we fall back to nondet).
    if(
      !obj_expr.is_nil() && obj_expr.type().id() == ID_struct &&
      to_struct_type(obj_expr.type()).get_tag() == "typescript_array" &&
      method == "sort")
    {
      exprt src = obj_expr;
      if(src.id() == ID_symbol)
      {
        const symbolt *s =
          symbol_table.lookup(to_symbol_expr(src).get_identifier());
        if(s && !s->value.is_nil())
          src = s->value;
      }
      if(src.id() == ID_struct && src.operands().size() >= 2)
      {
        mp_integer len{0};
        if(src.operands()[0].is_constant())
          to_integer(to_constant_expr(src.operands()[0]), len);
        const exprt &data = src.operands()[1];
        // Collect numeric values of the elements.
        std::vector<double> values;
        typet elem_type = double_type();
        bool all_constant = true;
        for(mp_integer i = 0; i < len; ++i)
        {
          auto idx = i.to_ulong();
          if(
            idx < data.operands().size() &&
            data.operands()[idx].is_constant() &&
            data.operands()[idx].type().id() == ID_floatbv)
          {
            elem_type = data.operands()[idx].type();
            ieee_floatt v{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            v.from_expr(to_constant_expr(data.operands()[idx]));
            values.push_back(std::stod(v.to_ansi_c_string()));
          }
          else
          {
            all_constant = false;
            break;
          }
        }
        // Determine sort direction from comparator.
        // Patterns we recognize:
        //   (a, b) => a - b     ascending (default for numbers)
        //   (a, b) => b - a     descending
        //   no comparator       ascending (by string conversion per spec,
        //                       but for numeric arrays we use numeric)
        bool descending = false;
        bool recognized = true;
        if(args.is_array() && !to_json_array(args).empty())
        {
          const jsont &cb = *to_json_array(args).begin();
          std::string cb_kind = json_string(json_member(cb, "_kind"));
          if(cb_kind == "ArrowFunction" || cb_kind == "FunctionExpression")
          {
            const jsont &body = json_member(cb, "body");
            // Body may be an expression (arrow) or a block.
            const jsont *expr_node = &body;
            if(json_string(json_member(body, "_kind")) == "Block")
            {
              // Take the first return statement's expression.
              const jsont &stmts = json_member(body, "statements");
              if(stmts.is_array())
              {
                for(const auto &st : to_json_array(stmts))
                {
                  if(json_string(json_member(st, "_kind")) == "ReturnStatement")
                  {
                    expr_node = &json_member(st, "expression");
                    break;
                  }
                }
              }
            }
            std::string ek = json_string(json_member(*expr_node, "_kind"));
            if(ek == "BinaryExpression")
            {
              std::string op = json_string(json_member(*expr_node, "operator"));
              std::string lhs = json_string(
                json_member(json_member(*expr_node, "left"), "text"));
              std::string rhs = json_string(
                json_member(json_member(*expr_node, "right"), "text"));
              // Identify parameter names.
              std::string p1, p2;
              const jsont &ps = json_member(cb, "parameters");
              if(ps.is_array())
              {
                auto pi = to_json_array(ps).begin();
                if(pi != to_json_array(ps).end())
                  p1 =
                    json_string(json_member(json_member(*pi, "name"), "text"));
                ++pi;
                if(pi != to_json_array(ps).end())
                  p2 =
                    json_string(json_member(json_member(*pi, "name"), "text"));
              }
              if(op == "MinusToken" && lhs == p1 && rhs == p2)
                descending = false;
              else if(op == "MinusToken" && lhs == p2 && rhs == p1)
                descending = true;
              else
                recognized = false;
            }
            else
              recognized = false;
          }
        }
        // If the array is constant and the comparator is recognized,
        // sort at conversion time.
        if(all_constant && recognized)
        {
          if(descending)
            std::sort(values.begin(), values.end(), std::greater<double>{});
          else
            std::sort(values.begin(), values.end());
          // Build sorted struct_exprt.
          exprt::operandst sorted_ops;
          for(double v : values)
          {
            ieee_floatt fv{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            fv.from_double(v);
            sorted_ops.push_back(fv.to_expr());
          }
          std::size_t actual = sorted_ops.size();
          std::size_t max_len = TYPESCRIPT_MAX_ARRAY_LENGTH;
          while(sorted_ops.size() < max_len)
            sorted_ops.push_back(from_integer(0, elem_type));
          array_typet arr_type{
            elem_type, from_integer(max_len, signedbv_typet{64})};
          struct_typet list_type = make_array_struct_type(arr_type);
          struct_exprt sorted_struct{
            {from_integer(actual, signedbv_typet{64}),
             array_exprt{std::move(sorted_ops), arr_type}},
            list_type};
          // sort() mutates the array in place. If the receiver is a
          // symbol, emit an assignment to update it.
          if(obj_expr.id() == ID_symbol)
          {
            pending_stmts.push_back(
              code_frontend_assignt{obj_expr, sorted_struct});
            // Also update the stored symbol value so subsequent
            // conversion-time reads see the sorted contents.
            const symbolt *obj_sym =
              symbol_table.lookup(to_symbol_expr(obj_expr).get_identifier());
            if(obj_sym != nullptr)
            {
              symbolt updated = *obj_sym;
              updated.value = sorted_struct;
              symbol_table.get_writeable(obj_sym->name)->value = sorted_struct;
            }
          }
          return sorted_struct;
        }
        // Fallthrough: return unsorted original (preserves behaviour
        // for symbolic arrays and unrecognized comparators).
        return obj_expr;
      }
    }
    // ES2024 sec-array.prototype.reverse
    // Array.reverse: return reversed copy
    if(
      !obj_expr.is_nil() && obj_expr.type().id() == ID_struct &&
      to_struct_type(obj_expr.type()).get_tag() == "typescript_array" &&
      method == "reverse")
    {
      exprt src = obj_expr;
      if(src.id() == ID_symbol)
      {
        const symbolt *s =
          symbol_table.lookup(to_symbol_expr(src).get_identifier());
        if(s && !s->value.is_nil())
          src = s->value;
      }
      if(src.id() == ID_struct && src.operands().size() >= 2)
      {
        mp_integer len{0};
        if(src.operands()[0].is_constant())
          to_integer(to_constant_expr(src.operands()[0]), len);
        const exprt &data = src.operands()[1];
        exprt::operandst reversed;
        typet elem_type = double_type();
        for(mp_integer i = len - 1; i >= 0; --i)
        {
          auto idx = i.to_ulong();
          if(idx < data.operands().size())
          {
            elem_type = data.operands()[idx].type();
            reversed.push_back(data.operands()[idx]);
          }
        }
        std::size_t actual = reversed.size();
        std::size_t max_len = TYPESCRIPT_MAX_ARRAY_LENGTH;
        while(reversed.size() < max_len)
          reversed.push_back(from_integer(0, elem_type));
        array_typet arr_type{
          elem_type, from_integer(max_len, signedbv_typet{64})};
        struct_typet list_type = make_array_struct_type(arr_type);
        return struct_exprt{
          {from_integer(actual, signedbv_typet{64}),
           array_exprt{std::move(reversed), arr_type}},
          list_type};
      }
    }
    // ES2024 sec-array.prototype.flat
    // Array.flat: for number[], returns itself (already flat)
    if(
      !obj_expr.is_nil() && obj_expr.type().id() == ID_struct &&
      to_struct_type(obj_expr.type()).get_tag() == "typescript_array" &&
      method == "flat")
    {
      exprt src = obj_expr;
      if(src.id() == ID_symbol)
      {
        const symbolt *s =
          symbol_table.lookup(to_symbol_expr(src).get_identifier());
        if(s && !s->value.is_nil())
          src = s->value;
      }
      return src;
    }
    // ES2024 sec-array.prototype.at
    // Array.at: access with index (supports negative)
    if(
      !obj_expr.is_nil() && obj_expr.type().id() == ID_struct &&
      to_struct_type(obj_expr.type()).get_tag() == "typescript_array" &&
      method == "at" && args.is_array() && !to_json_array(args).empty())
    {
      exprt idx_expr = convert_expression(*to_json_array(args).begin());
      exprt src = obj_expr;
      if(src.id() == ID_symbol)
      {
        const symbolt *s =
          symbol_table.lookup(to_symbol_expr(src).get_identifier());
        if(s && !s->value.is_nil())
          src = s->value;
      }
      if(src.id() == ID_struct && src.operands().size() >= 2)
      {
        mp_integer len{0};
        if(src.operands()[0].is_constant())
          to_integer(to_constant_expr(src.operands()[0]), len);
        const exprt &data = src.operands()[1];
        // Extract constant index (handles unary minus)
        int idx = 0;
        bool idx_const = false;
        if(idx_expr.is_constant() && idx_expr.type().id() == ID_floatbv)
        {
          ieee_floatt fv{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          fv.from_expr(to_constant_expr(idx_expr));
          idx = static_cast<int>(std::stod(fv.to_ansi_c_string()));
          idx_const = true;
        }
        else if(
          idx_expr.id() == ID_unary_minus &&
          idx_expr.operands()[0].is_constant())
        {
          ieee_floatt fv{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          fv.from_expr(to_constant_expr(idx_expr.operands()[0]));
          idx = -static_cast<int>(std::stod(fv.to_ansi_c_string()));
          idx_const = true;
        }
        if(idx_const)
        {
          if(idx < 0)
            idx = len.to_long() + idx;
          if(idx >= 0 && idx < static_cast<int>(data.operands().size()))
            return data.operands()[idx];
        }
        // Non-constant index: use index_exprt
        if(idx_expr.type() != signedbv_typet{64})
          idx_expr = typecast_exprt{idx_expr, signedbv_typet{64}};
        return index_exprt{data, idx_expr};
      }
      // Symbol array with non-constant value: use member + index access
      if(
        !src.is_nil() && src.type().id() == ID_struct &&
        to_struct_type(src.type()).get_tag() == "typescript_array")
      {
        const auto &st = to_struct_type(src.type());
        exprt data = member_exprt{src, "data", st.get_component("data").type()};
        if(idx_expr.type() != signedbv_typet{64})
          idx_expr = typecast_exprt{idx_expr, signedbv_typet{64}};
        return index_exprt{data, idx_expr};
      }
      return side_effect_expr_nondett{double_type(), get_location(node)};
    }
    // ES2024 sec-array.prototype.fill
    // Array.fill: fill array with value
    if(
      !obj_expr.is_nil() && obj_expr.type().id() == ID_struct &&
      to_struct_type(obj_expr.type()).get_tag() == "typescript_array" &&
      method == "fill" && args.is_array() && !to_json_array(args).empty())
    {
      exprt fill_val = convert_expression(*to_json_array(args).begin());
      exprt src = obj_expr;
      if(src.id() == ID_symbol)
      {
        const symbolt *s =
          symbol_table.lookup(to_symbol_expr(src).get_identifier());
        if(s && !s->value.is_nil())
          src = s->value;
      }
      if(src.id() == ID_struct && src.operands().size() >= 2)
      {
        mp_integer len{0};
        if(src.operands()[0].is_constant())
          to_integer(to_constant_expr(src.operands()[0]), len);
        exprt::operandst filled;
        for(mp_integer i = 0; i < len; ++i)
          filled.push_back(fill_val);
        std::size_t max_len = TYPESCRIPT_MAX_ARRAY_LENGTH;
        while(filled.size() < max_len)
          filled.push_back(from_integer(0, fill_val.type()));
        array_typet arr_type{
          fill_val.type(), from_integer(max_len, signedbv_typet{64})};
        struct_typet list_type = make_array_struct_type(arr_type);
        return struct_exprt{
          {from_integer(len.to_long(), signedbv_typet{64}),
           array_exprt{std::move(filled), arr_type}},
          list_type};
      }
    }
    // Array.concat: concatenate two arrays
    if(
      !obj_expr.is_nil() && obj_expr.type().id() == ID_struct &&
      to_struct_type(obj_expr.type()).get_tag() == "typescript_array" &&
      method == "concat" && args.is_array() && !to_json_array(args).empty())
    {
      exprt other = convert_expression(*to_json_array(args).begin());
      exprt src = obj_expr;
      if(src.id() == ID_symbol)
      {
        const symbolt *s =
          symbol_table.lookup(to_symbol_expr(src).get_identifier());
        if(s && !s->value.is_nil())
          src = s->value;
      }
      if(other.id() == ID_symbol)
      {
        const symbolt *s =
          symbol_table.lookup(to_symbol_expr(other).get_identifier());
        if(s && !s->value.is_nil())
          other = s->value;
      }
      if(
        src.id() == ID_struct && other.id() == ID_struct &&
        src.operands().size() >= 2 && other.operands().size() >= 2)
      {
        mp_integer len1{0}, len2{0};
        if(src.operands()[0].is_constant())
          to_integer(to_constant_expr(src.operands()[0]), len1);
        if(other.operands()[0].is_constant())
          to_integer(to_constant_expr(other.operands()[0]), len2);
        const exprt &d1 = src.operands()[1];
        const exprt &d2 = other.operands()[1];
        exprt::operandst combined;
        typet elem_type = double_type();
        for(mp_integer i = 0; i < len1; ++i)
        {
          auto idx = i.to_ulong();
          if(idx < d1.operands().size())
          {
            elem_type = d1.operands()[idx].type();
            combined.push_back(d1.operands()[idx]);
          }
        }
        for(mp_integer i = 0; i < len2; ++i)
        {
          auto idx = i.to_ulong();
          if(idx < d2.operands().size())
            combined.push_back(d2.operands()[idx]);
        }
        std::size_t actual = combined.size();
        std::size_t max_len = TYPESCRIPT_MAX_ARRAY_LENGTH;
        while(combined.size() < max_len)
          combined.push_back(from_integer(0, elem_type));
        array_typet arr_type{
          elem_type, from_integer(max_len, signedbv_typet{64})};
        struct_typet list_type = make_array_struct_type(arr_type);
        return struct_exprt{
          {from_integer(actual, signedbv_typet{64}),
           array_exprt{std::move(combined), arr_type}},
          list_type};
      }
    }
    // ES2024 sec-array.prototype.indexof
    // Array.indexOf: find index of element
    if(
      !obj_expr.is_nil() && obj_expr.type().id() == ID_struct &&
      to_struct_type(obj_expr.type()).get_tag() == "typescript_array" &&
      method == "indexOf" && args.is_array() && !to_json_array(args).empty())
    {
      exprt target = convert_expression(*to_json_array(args).begin());
      exprt src = obj_expr;
      if(src.id() == ID_symbol)
      {
        const symbolt *s =
          symbol_table.lookup(to_symbol_expr(src).get_identifier());
        if(s && !s->value.is_nil())
          src = s->value;
      }
      if(src.id() == ID_struct && src.operands().size() >= 2)
      {
        mp_integer len{0};
        if(src.operands()[0].is_constant())
          to_integer(to_constant_expr(src.operands()[0]), len);
        const exprt &data = src.operands()[1];
        // For constant arrays, find the index at conversion time
        if(target.is_constant())
        {
          for(mp_integer i = 0; i < len; ++i)
          {
            auto idx = i.to_ulong();
            if(idx < data.operands().size() && data.operands()[idx] == target)
            {
              double dv = static_cast<double>(i.to_long());
              uint64_t bits;
              std::memcpy(&bits, &dv, sizeof(bits));
              return constant_exprt{
                integer2bvrep(mp_integer{bits}, 64), double_type()};
            }
          }
          // Not found: return -1
          double neg1 = -1.0;
          uint64_t bits;
          std::memcpy(&bits, &neg1, sizeof(bits));
          return constant_exprt{
            integer2bvrep(mp_integer{bits}, 64), double_type()};
        }
      }
      return side_effect_expr_nondett{double_type(), get_location(node)};
    }
    // Array.every/some: boolean array predicates
    if(
      !obj_expr.is_nil() && obj_expr.type().id() == ID_struct &&
      to_struct_type(obj_expr.type()).get_tag() == "typescript_array" &&
      (method == "every" || method == "some") && args.is_array() &&
      !to_json_array(args).empty())
    {
      const jsont &callback = *to_json_array(args).begin();
      exprt src = obj_expr;
      if(src.id() == ID_symbol)
      {
        const symbolt *s =
          symbol_table.lookup(to_symbol_expr(src).get_identifier());
        if(s && !s->value.is_nil())
          src = s->value;
      }
      if(src.id() == ID_struct && src.operands().size() >= 2)
      {
        mp_integer len{0};
        if(src.operands()[0].is_constant())
          to_integer(to_constant_expr(src.operands()[0]), len);
        const exprt &data = src.operands()[1];
        static unsigned es_ctr = 0;
        unsigned fc = es_ctr++;
        std::string cb_name = "__ts_es_cb_" + std::to_string(fc);
        convert_function_declaration_with_name(callback, cb_name);
        irep_idt cb_id{"typescript::" + cb_name};
        // Result: every starts true, some starts false
        std::string res_name = "__ts_es_res_" + std::to_string(fc);
        irep_idt res_id{"typescript::" + res_name};
        {
          symbolt rs{res_id, bool_typet{}, "typescript"};
          rs.base_name = res_name;
          rs.is_lvalue = true;
          rs.is_state_var = true;
          if(symbol_table.lookup(res_id) == nullptr)
            symbol_table.add(rs);
        }
        pending_stmts.push_back(code_frontend_assignt{
          symbol_exprt{res_id, bool_typet{}},
          method == "every" ? exprt{true_exprt{}} : exprt{false_exprt{}}});
        for(mp_integer i = 0; i < len; ++i)
        {
          auto idx = i.to_ulong();
          if(idx >= data.operands().size())
            break;
          std::string p =
            "__ts_es_p_" + std::to_string(fc) + "_" + std::to_string(idx);
          irep_idt pid{"typescript::" + p};
          {
            symbolt ps{pid, bool_typet{}, "typescript"};
            ps.base_name = p;
            ps.is_lvalue = true;
            ps.is_state_var = true;
            if(symbol_table.lookup(pid) == nullptr)
              symbol_table.add(ps);
          }
          pending_stmts.push_back(code_frontend_assignt{
            symbol_exprt{pid, bool_typet{}},
            side_effect_expr_function_callt{
              symbol_exprt{cb_id, symbol_table.lookup_ref(cb_id).type},
              {data.operands()[idx]},
              bool_typet{},
              source_locationt{}}});
          if(method == "every")
          {
            // every: result = result && pred
            pending_stmts.push_back(code_frontend_assignt{
              symbol_exprt{res_id, bool_typet{}},
              and_exprt{
                symbol_exprt{res_id, bool_typet{}},
                symbol_exprt{pid, bool_typet{}}}});
          }
          else
          {
            // some: result = result || pred
            pending_stmts.push_back(code_frontend_assignt{
              symbol_exprt{res_id, bool_typet{}},
              or_exprt{
                symbol_exprt{res_id, bool_typet{}},
                symbol_exprt{pid, bool_typet{}}}});
          }
        }
        return symbol_exprt{res_id, bool_typet{}};
      }
    }
    // Array methods: push, pop
    if(!obj_expr.is_nil() && obj_expr.type().id() == ID_struct)
    {
      const auto &st = to_struct_type(obj_expr.type());
      if(st.get_tag() == "typescript_array" && st.has_component("data"))
      {
        if(method == "push" && args.is_array() && !to_json_array(args).empty())
        {
          exprt val = convert_expression(*to_json_array(args).begin());
          exprt len = member_exprt{obj_expr, "length", signedbv_typet{64}};
          exprt data =
            member_exprt{obj_expr, "data", st.get_component("data").type()};
          if(!val.is_nil())
          {
            typet et =
              to_array_type(st.get_component("data").type()).element_type();
            if(val.type() != et)
              val = typecast_exprt(val, et);
            pending_stmts.push_back(
              code_frontend_assignt{index_exprt{data, len}, val});
            pending_stmts.push_back(code_frontend_assignt{
              len, plus_exprt{len, from_integer(1, signedbv_typet{64})}});
          }
          return from_integer(0, double_type()); // push returns new length
        }
        if(method == "pop")
        {
          // ES2024 sec-array.prototype.pop: remove last element and
          // return it. We must evaluate data[length-1] BEFORE updating
          // length, otherwise the lazy expression tree re-reads the
          // updated length when the return value is assigned.
          exprt len = member_exprt{obj_expr, "length", signedbv_typet{64}};
          exprt data =
            member_exprt{obj_expr, "data", st.get_component("data").type()};
          // Create a temporary for the pre-decrement index.
          static unsigned pop_ctr = 0;
          std::string tmp_name =
            "typescript::pop_idx_" + std::to_string(pop_ctr++);
          irep_idt tmp_id{tmp_name};
          if(symbol_table.lookup(tmp_id) == nullptr)
          {
            symbolt tmp_sym{tmp_id, signedbv_typet{64}, "typescript"};
            tmp_sym.base_name = tmp_name;
            tmp_sym.is_lvalue = true;
            tmp_sym.is_state_var = true;
            tmp_sym.is_static_lifetime = true;
            symbol_table.add(tmp_sym);
          }
          symbol_exprt idx_expr{tmp_id, signedbv_typet{64}};
          // Capture (length - 1) into temp BEFORE updating length.
          pending_stmts.push_back(code_frontend_assignt{
            idx_expr, minus_exprt{len, from_integer(1, signedbv_typet{64})}});
          // Update length := length - 1.
          pending_stmts.push_back(code_frontend_assignt{
            len, minus_exprt{len, from_integer(1, signedbv_typet{64})}});
          // Return data[temp]. The index was captured before the update,
          // so this reads the correct last element.
          return index_exprt{data, idx_expr};
        }
        // ES2024 sec-array.prototype.splice
        if(method == "splice" && args.is_array())
        {
          const auto &arg_arr = to_json_array(args);
          if(arg_arr.size() >= 2)
          {
            auto it = arg_arr.begin();
            exprt start = convert_expression(*it++);
            exprt del_count = convert_expression(*it);
            exprt len = member_exprt{obj_expr, "length", signedbv_typet{64}};
            if(start.type() != signedbv_typet{64})
              start = typecast_exprt{start, signedbv_typet{64}};
            if(del_count.type() != signedbv_typet{64})
              del_count = typecast_exprt{del_count, signedbv_typet{64}};
            // Update length: length -= deleteCount
            pending_stmts.push_back(
              code_frontend_assignt{len, minus_exprt{len, del_count}});
          }
          // Return empty array (simplified — real splice returns removed)
          return obj_expr;
        }
        if(method == "length")
          return member_exprt{obj_expr, "length", signedbv_typet{64}};
      }
    }
    // ES2024 sec-promise.prototype.then, sec-promise.prototype.catch
    // Promise methods: then(fn) → fn(value), catch(fn) → value, finally(fn) → (fn(), value)
    if(method == "then" && args.is_array() && !to_json_array(args).empty())
    {
      const jsont &cb = *to_json_array(args).begin();
      static unsigned then_ctr = 0;
      std::string cb_name = "__ts_then_cb_" + std::to_string(then_ctr++);
      convert_function_declaration_with_name(cb, cb_name);
      irep_idt cb_id{"typescript::" + cb_name};
      const symbolt *cb_sym = symbol_table.lookup(cb_id);
      if(cb_sym != nullptr)
      {
        typet ret = to_code_type(cb_sym->type).return_type();
        return side_effect_expr_function_callt{
          cb_sym->symbol_expr(), {obj_expr}, ret, get_location(node)};
      }
    }
    if(method == "catch" || method == "finally")
    {
      // In synchronous model: catch is never triggered, finally runs but
      // doesn't affect the value
      if(method == "finally" && args.is_array() && !to_json_array(args).empty())
      {
        const jsont &cb = *to_json_array(args).begin();
        static unsigned finally_ctr = 0;
        std::string cb_name =
          "__ts_finally_cb_" + std::to_string(finally_ctr++);
        convert_function_declaration_with_name(cb, cb_name);
        irep_idt cb_id{"typescript::" + cb_name};
        const symbolt *cb_sym = symbol_table.lookup(cb_id);
        if(cb_sym != nullptr)
        {
          pending_stmts.push_back(
            code_expressiont{side_effect_expr_function_callt{
              cb_sym->symbol_expr(), {}, empty_typet{}, get_location(node)}});
        }
      }
      return obj_expr; // pass through the value
    }
    // ES2024 sec-map.prototype.set, sec-map.prototype.get, sec-map.prototype.has
    // Map/Set methods
    if(!obj_expr.is_nil() && obj_expr.type().id() == ID_struct)
    {
      const auto &mst = to_struct_type(obj_expr.type());
      std::string mtag = id2string(mst.get_tag());
      if(mtag == "typescript_class_Map" && mst.has_component("keys"))
      {
        exprt size_m = member_exprt{obj_expr, "size", signedbv_typet{64}};
        exprt keys_m =
          member_exprt{obj_expr, "keys", mst.get_component("keys").type()};
        exprt vals_m =
          member_exprt{obj_expr, "values", mst.get_component("values").type()};
        if(
          method == "set" && args.is_array() && to_json_array(args).size() >= 2)
        {
          auto it = to_json_array(args).begin();
          exprt key = convert_expression(*it++);
          exprt val = convert_expression(*it);
          // Cast key to match keys array element type
          const auto &keys_arr_type =
            to_array_type(mst.get_component("keys").type());
          if(key.type() != keys_arr_type.element_type())
            key = typecast_exprt{key, keys_arr_type.element_type()};
          if(val.type() != double_type())
            val = typecast_exprt{val, double_type()};
          pending_stmts.push_back(
            code_frontend_assignt{index_exprt{keys_m, size_m}, key});
          pending_stmts.push_back(
            code_frontend_assignt{index_exprt{vals_m, size_m}, val});
          pending_stmts.push_back(code_frontend_assignt{
            size_m, plus_exprt{size_m, from_integer(1, signedbv_typet{64})}});
          return obj_expr; // set returns the Map
        }
        if(method == "get" && args.is_array() && !to_json_array(args).empty())
        {
          exprt key = convert_expression(*to_json_array(args).begin());
          const auto &keys_arr_type2 =
            to_array_type(mst.get_component("keys").type());
          if(key.type() != keys_arr_type2.element_type())
            key = typecast_exprt{key, keys_arr_type2.element_type()};
          // Linear scan: result = values[i] where keys[i] == key
          exprt result =
            side_effect_expr_nondett{double_type(), get_location(node)};
          for(int i = 7; i >= 0; i--)
          {
            exprt idx = from_integer(i, signedbv_typet{64});
            exprt in_range = binary_relation_exprt{idx, ID_lt, size_m};
            exprt key_i = index_exprt{keys_m, idx};
            exprt match = equal_exprt{key_i, key};
            exprt cond = and_exprt{in_range, match};
            result = if_exprt{cond, index_exprt{vals_m, idx}, result};
          }
          return result;
        }
        if(method == "has" && args.is_array() && !to_json_array(args).empty())
        {
          exprt key = convert_expression(*to_json_array(args).begin());
          const auto &keys_arr_type3 =
            to_array_type(mst.get_component("keys").type());
          if(key.type() != keys_arr_type3.element_type())
            key = typecast_exprt{key, keys_arr_type3.element_type()};
          exprt result = false_exprt{};
          for(int i = 7; i >= 0; i--)
          {
            exprt idx = from_integer(i, signedbv_typet{64});
            exprt in_range = binary_relation_exprt{idx, ID_lt, size_m};
            exprt key_i = index_exprt{keys_m, idx};
            exprt match = equal_exprt{key_i, key};
            result = or_exprt{result, and_exprt{in_range, match}};
          }
          return result;
        }
        if(method == "delete")
        {
          pending_stmts.push_back(code_frontend_assignt{
            size_m, minus_exprt{size_m, from_integer(1, signedbv_typet{64})}});
          return true_exprt{};
        }
      }
      if(mtag == "typescript_class_Set" && mst.has_component("data"))
      {
        exprt size_s = member_exprt{obj_expr, "size", signedbv_typet{64}};
        exprt data_s =
          member_exprt{obj_expr, "data", mst.get_component("data").type()};
        if(method == "add" && args.is_array() && !to_json_array(args).empty())
        {
          exprt val = convert_expression(*to_json_array(args).begin());
          if(val.type() != double_type())
            val = typecast_exprt{val, double_type()};
          pending_stmts.push_back(
            code_frontend_assignt{index_exprt{data_s, size_s}, val});
          pending_stmts.push_back(code_frontend_assignt{
            size_s, plus_exprt{size_s, from_integer(1, signedbv_typet{64})}});
          return obj_expr;
        }
        if(method == "has" && args.is_array() && !to_json_array(args).empty())
        {
          // ES2024 sec-set.prototype.has: linear scan of data[0..size)
          // mirrors the Map.has implementation above.
          exprt val = convert_expression(*to_json_array(args).begin());
          const auto &data_arr_type =
            to_array_type(mst.get_component("data").type());
          if(val.type() != data_arr_type.element_type())
            val = typecast_exprt{val, data_arr_type.element_type()};
          exprt result = false_exprt{};
          for(int i = 7; i >= 0; i--)
          {
            exprt idx = from_integer(i, signedbv_typet{64});
            exprt in_range = binary_relation_exprt{idx, ID_lt, size_s};
            exprt data_i = index_exprt{data_s, idx};
            exprt match = equal_exprt{data_i, val};
            result = or_exprt{result, and_exprt{in_range, match}};
          }
          return result;
        }
        if(method == "delete")
        {
          pending_stmts.push_back(code_frontend_assignt{
            size_s, minus_exprt{size_s, from_integer(1, signedbv_typet{64})}});
          return true_exprt{};
        }
      }
    }
    // Check for class method calls
    if(!obj_expr.is_nil() && obj_expr.type().id() == ID_struct)
    {
      const auto &st = to_struct_type(obj_expr.type());
      std::string tag = id2string(st.get_tag());
      if(tag.substr(0, 17) == "typescript_class_")
      {
        std::string cls = tag.substr(17);
        irep_idt method_id{"typescript::" + cls + "::" + method};
        const symbolt *msym = symbol_table.lookup(method_id);
        // Check parent class if method not found
        if(msym == nullptr || msym->type.id() != ID_code)
        {
          std::string parent = cls;
          while(parent_class.count(parent) > 0)
          {
            parent = parent_class[parent];
            irep_idt pid{"typescript::" + parent + "::" + method};
            msym = symbol_table.lookup(pid);
            if(msym != nullptr && msym->type.id() == ID_code)
              break;
          }
        }
        if(msym != nullptr && msym->type.id() == ID_code)
        {
          exprt::operandst margs;
          margs.push_back(address_of_exprt{obj_expr});
          if(args.is_array())
            for(const auto &a : to_json_array(args))
              margs.push_back(convert_expression(a));
          const auto &mparams = to_code_type(msym->type).parameters();
          for(std::size_t i = 0; i < margs.size() && i < mparams.size(); i++)
            if(margs[i].type() != mparams[i].type())
              margs[i] = typecast_exprt(margs[i], mparams[i].type());
          return side_effect_expr_function_callt{
            msym->symbol_expr(),
            std::move(margs),
            to_code_type(msym->type).return_type(),
            get_location(node)};
        }
      }
    }
    // Handle Math.* built-in functions
    if(obj_name == "Math" && args.is_array())
    {
      // Math.random() — no args
      if(method == "random")
        return side_effect_expr_nondett{double_type(), get_location(node)};
      // ES2024 sec-math.*: constant evaluation at conversion time
      std::vector<double> arg_vals;
      bool all_const = true;
      for(const auto &a : to_json_array(args))
      {
        exprt val = convert_expression(a);
        // Try to extract constant double
        const exprt *ce = &val;
        if(ce->id() == ID_typecast && ce->operands().size() == 1)
          ce = &ce->operands()[0];
        // Handle unary minus on constant: -5 → constant
        if(ce->id() == ID_unary_minus && ce->operands().size() == 1)
        {
          const exprt *inner = &ce->operands()[0];
          if(inner->id() == ID_typecast && inner->operands().size() == 1)
            inner = &inner->operands()[0];
          if(inner->is_constant() && inner->type().id() == ID_floatbv)
          {
            ieee_floatt fv{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            fv.from_expr(to_constant_expr(*inner));
            arg_vals.push_back(-std::stod(fv.to_ansi_c_string()));
            continue;
          }
        }
        if(ce->is_constant() && ce->type().id() == ID_floatbv)
        {
          ieee_floatt fv{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          fv.from_expr(to_constant_expr(*ce));
          arg_vals.push_back(std::stod(fv.to_ansi_c_string()));
        }
        else
          all_const = false;
      }
      if(all_const && !arg_vals.empty())
      {
        double res = 0;
        bool ok = true;
        if(method == "sqrt" && arg_vals[0] >= 0)
          res = std::sqrt(arg_vals[0]);
        else if(method == "abs")
          res = std::fabs(arg_vals[0]);
        else if(method == "floor")
          res = std::floor(arg_vals[0]);
        else if(method == "ceil")
          res = std::ceil(arg_vals[0]);
        else if(method == "round")
          res = std::round(arg_vals[0]);
        else if(method == "sin")
          res = std::sin(arg_vals[0]);
        else if(method == "cos")
          res = std::cos(arg_vals[0]);
        else if(method == "log")
          res = std::log(arg_vals[0]);
        else if(method == "exp")
          res = std::exp(arg_vals[0]);
        else if(method == "pow" && arg_vals.size() >= 2)
          res = std::pow(arg_vals[0], arg_vals[1]);
        else if(method == "max" && arg_vals.size() >= 2)
        {
          res = arg_vals[0];
          for(std::size_t i = 1; i < arg_vals.size(); i++)
            res = std::max(res, arg_vals[i]);
        }
        else if(method == "min" && arg_vals.size() >= 2)
        {
          res = arg_vals[0];
          for(std::size_t i = 1; i < arg_vals.size(); i++)
            res = std::min(res, arg_vals[i]);
        }
        else
          ok = false;
        if(ok)
        {
          uint64_t bits;
          std::memcpy(&bits, &res, sizeof(bits));
          std::string bs = std::to_string(bits);
          return constant_exprt{
            integer2bvrep(mp_integer{bs.c_str()}, 64), double_type()};
        }
      }
      // Symbolic Math operations for non-constant args
      if(!to_json_array(args).empty())
      {
        exprt arg0 = convert_expression(*to_json_array(args).begin());
        if(!arg0.is_nil())
        {
          if(arg0.type() != double_type())
            arg0 = typecast_exprt{arg0, double_type()};
          if(method == "abs")
          {
            uint64_t zbits;
            double zero = 0.0;
            std::memcpy(&zbits, &zero, sizeof(zbits));
            exprt fzero = constant_exprt{
              integer2bvrep(mp_integer{std::to_string(zbits).c_str()}, 64),
              double_type()};
            return if_exprt{
              binary_relation_exprt{arg0, ID_ge, fzero},
              arg0,
              unary_minus_exprt{arg0}};
          }
        }
      }
      // Nondet fallback
      return side_effect_expr_nondett{double_type(), get_location(node)};
    }
  }
  // Handle regular function calls
  if(is_kind(callee, "Identifier"))
  {
    std::string func_name = json_string(json_member(callee, "text"));

    // Generic function monomorphization
    auto gen_it = generic_functions.find(func_name);
    if(gen_it != generic_functions.end())
    {
      // Get type parameter names first
      std::set<std::string> tp_names;
      const jsont &tp0 = json_member(gen_it->second, "typeParameters");
      if(tp0.is_array())
        for(const auto &t : to_json_array(tp0))
        {
          std::string n = json_string(json_member(t, "_type"));
          if(!n.empty())
            tp_names.insert(n);
        }
      std::string tp_name = tp_names.empty() ? "T" : *tp_names.begin();
      // Determine concrete type:
      //   1. Check if return type mentions the type parameter → use return type
      //   2. Otherwise, infer from first argument that has T as its type
      std::string call_type;
      std::string ret_type_str =
        json_string(json_member(gen_it->second, "_returnType"));
      if(ret_type_str == tp_name)
      {
        // Return type is the type parameter → call's _type is concrete T
        call_type = json_string(json_member(node, "_type"));
      }
      else
      {
        // Find argument with type T and use its concrete type
        const jsont &params = json_member(gen_it->second, "parameters");
        if(params.is_array() && args.is_array())
        {
          auto param_it = to_json_array(params).begin();
          auto arg_it = to_json_array(args).begin();
          while(param_it != to_json_array(params).end() &&
                arg_it != to_json_array(args).end())
          {
            std::string pt = json_string(json_member(*param_it, "_type"));
            if(pt == tp_name)
            {
              call_type = json_string(json_member(*arg_it, "_type"));
              break;
            }
            ++param_it;
            ++arg_it;
          }
        }
        if(call_type.empty())
          call_type = json_string(json_member(node, "_type"));
      }
      if(call_type.empty())
        call_type = "number";
      // Normalize literal types to base types
      if(call_type == "true" || call_type == "false")
        call_type = "boolean";
      else if(
        !call_type.empty() &&
        (std::isdigit(call_type[0]) || call_type[0] == '-'))
        call_type = "number";
      // Sanitize name — strip spaces, replace special chars
      std::string safe_type = call_type;
      for(char &c : safe_type)
      {
        if(
          c == ' ' || c == '{' || c == '}' || c == ':' || c == ';' ||
          c == '<' || c == '>' || c == ',' || c == '(' || c == ')' ||
          c == '|' || c == '[' || c == ']')
          c = '_';
      }
      // Create specialized instance name
      std::string spec_name = func_name + "__" + safe_type;
      irep_idt spec_id{"typescript::" + spec_name};
      // Instantiate if not already done
      if(symbol_table.lookup(spec_id) == nullptr)
      {
        // Convert the generic function with concrete types
        const jsont &gen_node = gen_it->second;
        jsont saved_node = gen_node;
        generic_functions.erase(gen_it);
        // Store the concrete type mapping for this instantiation
        std::string saved_generic_type_param = current_generic_type_param;
        std::string saved_generic_concrete = current_generic_concrete;
        current_generic_type_param = tp_name;
        current_generic_concrete = call_type;
        convert_function_declaration_with_name(saved_node, spec_name);
        current_generic_type_param = saved_generic_type_param;
        current_generic_concrete = saved_generic_concrete;
        // Restore
        generic_functions[func_name] = saved_node;
      }
      // Call the specialized function
      exprt::operandst call_args;
      if(args.is_array())
        for(const auto &a : to_json_array(args))
          call_args.push_back(convert_expression(a));
      const symbolt &fn = symbol_table.lookup_ref(spec_id);
      typet ret = to_code_type(fn.type).return_type();
      side_effect_expr_function_callt call{
        symbol_exprt{spec_id, fn.type},
        std::move(call_args),
        ret,
        get_location(node)};
      return std::move(call);
    }

    // Verification primitives
    if(func_name == "nondet_number")
      return side_effect_expr_nondett{double_type(), get_location(node)};
    if(func_name == "nondet_boolean")
      return side_effect_expr_nondett{bool_typet{}, get_location(node)};
    if(func_name == "nondet_string")
      return side_effect_expr_nondett{
        typescript_string_type(), get_location(node)};
    if(func_name == "nondet_array")
    {
      // Create array with nondet elements
      typet elem_type = double_type();
      std::size_t max_len = TYPESCRIPT_MAX_ARRAY_LENGTH;
      exprt::operandst elts;
      for(std::size_t i = 0; i < max_len; ++i)
        elts.push_back(side_effect_expr_nondett{elem_type, get_location(node)});
      array_typet arr_type{
        elem_type, from_integer(max_len, signedbv_typet{64})};
      struct_typet list_type;
      list_type.components().push_back(
        struct_typet::componentt{"length", signedbv_typet{64}});
      list_type.components().push_back(
        struct_typet::componentt{"data", arr_type});
      list_type.set_tag("typescript_array");
      return struct_exprt{
        {side_effect_expr_nondett{signedbv_typet{64}, get_location(node)},
         array_exprt{std::move(elts), arr_type}},
        list_type};
    }
    if(func_name == "parseInt" || func_name == "parseFloat")
    {
      // For constant string args, parse at conversion time
      if(args.is_array() && !to_json_array(args).empty())
      {
        exprt arg = convert_expression(*to_json_array(args).begin());
        std::string sv = extract_string_value(arg);
        if(!sv.empty())
        {
          try
          {
            double d = std::stod(sv.substr(2));
            if(func_name == "parseInt")
              d = std::floor(d);
            return from_integer(0, double_type()); // placeholder
          }
          catch(...)
          {
          }
        }
      }
      return side_effect_expr_nondett{double_type(), get_location(node)};
    }
    if(func_name == "__CPROVER_assume")
    {
      // Handled at statement level
      return nil_exprt{};
    }
    if(func_name == "__CPROVER_assert")
    {
      // Handled at statement level
      return nil_exprt{};
    }
    if(func_name == "__CPROVER_havoc_object")
    {
      // Handled at statement level
      return nil_exprt{};
    }
    if(func_name == "__CPROVER_cover")
    {
      // Handled at statement level
      return nil_exprt{};
    }

    // Regular function call
    irep_idt func_id{"typescript::" + func_name};
    const symbolt *sym = symbol_table.lookup(func_id);

    // Check for function-typed variable/parameter (higher-order function)
    if(sym == nullptr || sym->type.id() != ID_code)
    {
      // Try as local variable or module-level variable
      const symbolt *local_sym = nullptr;
      if(!current_function.empty())
      {
        irep_idt local_id{"typescript::" + current_function + "::" + func_name};
        local_sym = symbol_table.lookup(local_id);
      }
      if(local_sym == nullptr)
      {
        irep_idt global_id{"typescript::" + func_name};
        local_sym = symbol_table.lookup(global_id);
      }
      if(
        local_sym != nullptr &&
        (local_sym->type.id() == ID_pointer || local_sym->type.id() == ID_code))
      {
        // This is a function-typed parameter — emit indirect call
        exprt::operandst arguments;
        if(args.is_array())
        {
          for(const auto &arg : to_json_array(args))
            arguments.push_back(convert_expression(arg));
        }
        // Determine return type from the pointer/code type
        typet ret_type = double_type();
        typet callee_type = local_sym->type;
        if(callee_type.id() == ID_pointer)
          callee_type = to_pointer_type(callee_type).base_type();
        if(callee_type.id() == ID_code)
          ret_type = to_code_type(callee_type).return_type();
        // Check for closure binding
        auto cb_it = closure_bindings.find(local_sym->name);
        if(cb_it != closure_bindings.end())
        {
          // Call the bound function with original args + captured values
          const auto &binding = cb_it->second;
          const symbolt *fn = symbol_table.lookup(binding.function_id);
          if(fn != nullptr)
          {
            exprt::operandst full_args = arguments;
            for(const auto &bv : binding.bound_values)
              full_args.push_back(bv);
            typet fn_ret = to_code_type(fn->type).return_type();
            return side_effect_expr_function_callt{
              fn->symbol_expr(),
              std::move(full_args),
              fn_ret,
              get_location(node)};
          }
        }
        // Dereference if pointer
        exprt callee_expr = local_sym->symbol_expr();
        if(local_sym->type.id() == ID_pointer)
          callee_expr = dereference_exprt{callee_expr};
        return side_effect_expr_function_callt{
          callee_expr, std::move(arguments), ret_type, get_location(node)};
      }
    }
    if(sym != nullptr && sym->type.id() == ID_code)
    {
      const code_typet &func_type = to_code_type(sym->type);
      exprt::operandst arguments;
      if(args.is_array())
      {
        for(const auto &arg : to_json_array(args))
        {
          // ES2024 sec-runtime-semantics-argumentlistevaluation
          // Handle spread: fn(...arr) expands array into individual args
          if(is_kind(arg, "SpreadElement"))
          {
            exprt src = convert_expression(json_member(arg, "expression"));
            if(src.id() == ID_symbol)
            {
              const symbolt *s =
                symbol_table.lookup(to_symbol_expr(src).get_identifier());
              if(s && !s->value.is_nil())
                src = s->value;
            }
            if(src.id() == ID_struct && src.operands().size() >= 2)
            {
              mp_integer len{0};
              if(src.operands()[0].is_constant())
                to_integer(to_constant_expr(src.operands()[0]), len);
              const exprt &data = src.operands()[1];
              for(mp_integer i = 0; i < len; ++i)
              {
                auto idx = i.to_ulong();
                if(idx < data.operands().size())
                  arguments.push_back(data.operands()[idx]);
              }
            }
            else if(
              !src.is_nil() && src.type().id() == ID_struct &&
              to_struct_type(src.type()).get_tag() == "typescript_array")
            {
              // Runtime array: use index access
              const auto &st = to_struct_type(src.type());
              exprt data_e =
                member_exprt{src, "data", st.get_component("data").type()};
              const auto &fp2 = func_type.parameters();
              for(std::size_t pi = arguments.size(); pi < fp2.size(); ++pi)
                arguments.push_back(index_exprt{
                  data_e,
                  from_integer(pi - arguments.size(), signedbv_typet{64})});
            }
            continue;
          }
          exprt val = convert_expression(arg);
          // If argument is a function symbol, take its address
          if(val.id() == ID_symbol && val.type().id() == ID_code)
          {
            val = address_of_exprt{val};
          }
          arguments.push_back(val);
        }
      }
      // Typecast null args to match parameter types
      const auto &fp = func_type.parameters();
      std::size_t typecast_limit = fp.size();
      if(rest_param_functions.count(func_id) > 0 && typecast_limit > 0)
        typecast_limit--; // don't typecast rest args individually
      for(std::size_t i = 0; i < arguments.size() && i < typecast_limit; i++)
      {
        if(arguments[i].type() != fp[i].type())
        {
          // null (int 0) → zero-length string
          if(
            is_typescript_string_type(fp[i].type()) &&
            arguments[i].type().id() == ID_signedbv)
            arguments[i] = convert_string_literal_from_text("");
          else if(
            fp[i].type().id() == ID_struct &&
            to_struct_type(fp[i].type()).get_tag() == "typescript_union")
          {
            // Wrap value into union struct
            const auto &union_st = to_struct_type(fp[i].type());
            int tag = 0;
            for(std::size_t c = 1; c < union_st.components().size(); ++c)
            {
              if(union_st.components()[c].type() == arguments[i].type())
              {
                tag = static_cast<int>(c - 1);
                break;
              }
            }
            exprt::operandst fields;
            fields.push_back(from_integer(tag, signedbv_typet{32}));
            for(std::size_t c = 1; c < union_st.components().size(); ++c)
            {
              if(static_cast<int>(c - 1) == tag)
                fields.push_back(arguments[i]);
              else
                fields.push_back(side_effect_expr_nondett{
                  union_st.components()[c].type(), source_locationt{}});
            }
            arguments[i] = struct_exprt{std::move(fields), fp[i].type()};
          }
          else if(arguments[i].type().id() != fp[i].type().id())
            arguments[i] = typecast_exprt(arguments[i], fp[i].type());
        }
      }
      // Fill captured variable args (only for parameter captures)
      if(captured_var_map.count(func_id) > 0)
      {
        for(const auto &[cv_name, cv_outer_id] : captured_var_map[func_id])
        {
          const symbolt *cv_sym = symbol_table.lookup(cv_outer_id);
          if(cv_sym != nullptr && cv_sym->is_parameter)
            arguments.push_back(cv_sym->symbol_expr());
        }
      }
      // Fill missing args with defaults (skip rest param)
      std::size_t fill_limit = fp.size();
      if(rest_param_functions.count(func_id) > 0 && fill_limit > 0)
        fill_limit--;
      auto def_it = default_values.find(func_id);
      while(arguments.size() < fill_limit)
      {
        std::size_t idx = arguments.size();
        if(def_it != default_values.end())
        {
          auto val_it = def_it->second.find(idx);
          if(val_it != def_it->second.end())
          {
            arguments.push_back(val_it->second);
            continue;
          }
        }
        arguments.push_back(
          side_effect_expr_nondett{fp[idx].type(), get_location(node)});
      }
      // Pack extra args into array for rest param functions
      if(rest_param_functions.count(func_id) > 0 && !fp.empty())
      {
        std::size_t regular_count = fp.size() - 1;
        if(arguments.size() >= regular_count)
        {
          exprt::operandst rest_elts;
          typet elem_type = double_type();
          for(std::size_t i = regular_count; i < arguments.size(); ++i)
          {
            elem_type = arguments[i].type();
            rest_elts.push_back(arguments[i]);
          }
          arguments.resize(regular_count);
          std::size_t actual = rest_elts.size();
          std::size_t max_len = TYPESCRIPT_MAX_ARRAY_LENGTH;
          while(rest_elts.size() < max_len)
            rest_elts.push_back(from_integer(0, elem_type));
          array_typet arr_type{
            elem_type, from_integer(max_len, signedbv_typet{64})};
          struct_typet list_type = make_array_struct_type(arr_type);
          struct_exprt arr{
            {from_integer(actual, signedbv_typet{64}),
             array_exprt{std::move(rest_elts), arr_type}},
            list_type};
          if(arr.type() != fp.back().type())
            arguments.push_back(typecast_exprt{arr, fp.back().type()});
          else
            arguments.push_back(arr);
        }
      }
      return side_effect_expr_function_callt{
        sym->symbol_expr(),
        std::move(arguments),
        func_type.return_type(),
        get_location(node)};
    }

    log.warning() << "Unknown function: '" << func_name
                  << "' (not declared in this module, not a built-in, "
                  << "and not imported); returning nondet" << messaget::eom;
    return side_effect_expr_nondett{double_type(), get_location(node)};
  }

  return side_effect_expr_nondett{double_type(), get_location(node)};
}

// --- Statement conversion ---

bool typescript_convertert::convert()
{
  // Register built-in classes
  {
    // Error class: { message: string }
    struct_typet error_type;
    error_type.components().push_back(
      struct_typet::componentt{"message", typescript_string_type()});
    error_type.set_tag("typescript_class_Error");
    class_types["Error"] = error_type;
    // Register Error constructor
    std::string ctor_name = "Error::__init__";
    irep_idt ctor_id{"typescript::" + ctor_name};
    if(symbol_table.lookup(ctor_id) == nullptr)
    {
      code_typet::parameterst params;
      code_typet::parametert this_p{pointer_typet{error_type, 64}};
      this_p.set_identifier("typescript::" + ctor_name + "::this");
      this_p.set_base_name("this");
      params.push_back(this_p);
      code_typet::parametert msg_p{typescript_string_type()};
      msg_p.set_identifier("typescript::" + ctor_name + "::message");
      msg_p.set_base_name("message");
      params.push_back(msg_p);
      code_typet ft{params, empty_typet{}};
      symbolt ctor_sym{ctor_id, ft, "typescript"};
      ctor_sym.base_name = ctor_name;
      // Body: this.message = message
      irep_idt this_id{"typescript::" + ctor_name + "::this"};
      irep_idt msg_id{"typescript::" + ctor_name + "::message"};
      symbolt this_sym{this_id, pointer_typet{error_type, 64}, "typescript"};
      this_sym.base_name = "this";
      this_sym.is_parameter = true;
      this_sym.is_lvalue = true;
      symbolt msg_sym{msg_id, typescript_string_type(), "typescript"};
      msg_sym.base_name = "message";
      msg_sym.is_parameter = true;
      msg_sym.is_lvalue = true;
      if(symbol_table.lookup(this_id) == nullptr)
        symbol_table.add(this_sym);
      if(symbol_table.lookup(msg_id) == nullptr)
        symbol_table.add(msg_sym);
      ctor_sym.value = code_frontend_assignt{
        member_exprt{
          dereference_exprt{
            symbol_exprt{this_id, pointer_typet{error_type, 64}}},
          "message",
          typescript_string_type()},
        symbol_exprt{msg_id, typescript_string_type()}};
      symbol_table.add(ctor_sym);
    }
  }

  // Map class: { size: signedbv[64], keys: double[8], values: double[8] }
  {
    std::size_t max_map = 8;
    array_typet keys_type{
      typescript_string_type(), from_integer(max_map, signedbv_typet{64})};
    array_typet vals_type{
      double_type(), from_integer(max_map, signedbv_typet{64})};
    struct_typet map_type;
    map_type.components().push_back(
      struct_typet::componentt{"size", signedbv_typet{64}});
    map_type.components().push_back(
      struct_typet::componentt{"keys", keys_type});
    map_type.components().push_back(
      struct_typet::componentt{"values", vals_type});
    map_type.set_tag("typescript_class_Map");
    class_types["Map"] = map_type;
    std::string ctor_name = "Map::__init__";
    irep_idt ctor_id{"typescript::" + ctor_name};
    if(symbol_table.lookup(ctor_id) == nullptr)
    {
      code_typet::parameterst params;
      code_typet::parametert this_p{pointer_typet{map_type, 64}};
      this_p.set_identifier("typescript::" + ctor_name + "::this");
      this_p.set_base_name("this");
      params.push_back(this_p);
      code_typet ft{params, empty_typet{}};
      symbolt ctor_sym{ctor_id, ft, "typescript"};
      ctor_sym.base_name = ctor_name;
      irep_idt this_id{"typescript::" + ctor_name + "::this"};
      symbolt this_sym{this_id, pointer_typet{map_type, 64}, "typescript"};
      this_sym.base_name = "this";
      this_sym.is_parameter = true;
      this_sym.is_lvalue = true;
      if(symbol_table.lookup(this_id) == nullptr)
        symbol_table.add(this_sym);
      ctor_sym.value = code_frontend_assignt{
        member_exprt{
          dereference_exprt{symbol_exprt{this_id, pointer_typet{map_type, 64}}},
          "size",
          signedbv_typet{64}},
        from_integer(0, signedbv_typet{64})};
      symbol_table.add(ctor_sym);
    }
  }
  // Set class: { size: signedbv[64], data: double[8] }
  {
    std::size_t max_set = 8;
    array_typet data_type{
      double_type(), from_integer(max_set, signedbv_typet{64})};
    struct_typet set_type;
    set_type.components().push_back(
      struct_typet::componentt{"size", signedbv_typet{64}});
    set_type.components().push_back(
      struct_typet::componentt{"data", data_type});
    set_type.set_tag("typescript_class_Set");
    class_types["Set"] = set_type;
    std::string ctor_name = "Set::__init__";
    irep_idt ctor_id{"typescript::" + ctor_name};
    if(symbol_table.lookup(ctor_id) == nullptr)
    {
      code_typet::parameterst params;
      code_typet::parametert this_p{pointer_typet{set_type, 64}};
      this_p.set_identifier("typescript::" + ctor_name + "::this");
      this_p.set_base_name("this");
      params.push_back(this_p);
      code_typet ft{params, empty_typet{}};
      symbolt ctor_sym{ctor_id, ft, "typescript"};
      ctor_sym.base_name = ctor_name;
      irep_idt this_id{"typescript::" + ctor_name + "::this"};
      symbolt this_sym{this_id, pointer_typet{set_type, 64}, "typescript"};
      this_sym.base_name = "this";
      this_sym.is_parameter = true;
      this_sym.is_lvalue = true;
      if(symbol_table.lookup(this_id) == nullptr)
        symbol_table.add(this_sym);
      ctor_sym.value = code_frontend_assignt{
        member_exprt{
          dereference_exprt{symbol_exprt{this_id, pointer_typet{set_type, 64}}},
          "size",
          signedbv_typet{64}},
        from_integer(0, signedbv_typet{64})};
      symbol_table.add(ctor_sym);
    }
  }

  const jsont &statements = json_member(ast_json, "statements");
  convert_module_body(statements);
  return false; // success
}
