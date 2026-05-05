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
#include <util/string_expr.h>
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
    // Object static methods
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
          struct_typet list_type;
          list_type.components().push_back(
            struct_typet::componentt{"length", signedbv_typet{64}});
          list_type.components().push_back(
            struct_typet::componentt{"data", arr_type});
          list_type.set_tag("typescript_array");
          return struct_exprt{
            {from_integer(actual, signedbv_typet{64}),
             array_exprt{std::move(elts), arr_type}},
            list_type};
        }
      }
      return side_effect_expr_nondett{double_type(), get_location(node)};
    }
    // Number static methods
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

  // Handle super() calls — call parent constructor
  if(is_kind(callee, "SuperKeyword") && !current_class.empty())
  {
    // Find parent by checking which class's fields are a subset
    exprt::operandst call_args;
    if(args.is_array())
    {
      for(const auto &arg : to_json_array(args))
        call_args.push_back(convert_expression(arg));
    }
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
    // String methods: indexOf, includes, substring, etc.
    exprt obj_expr = convert_expression(json_member(callee, "expression"));
    if(!obj_expr.is_nil() && is_typescript_string_type(obj_expr.type()))
    {
      // Try to get constant string value
      std::string sv;
      {
        std::string raw = extract_string_value(obj_expr);
        if(!raw.empty())
          sv = raw.substr(2);
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
      if(!sv.empty())
      {
        if(method == "indexOf" && !str_args.empty())
        {
          auto pos = sv.find(str_args[0]);
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
          int start = num_args[0], end = num_args[1];
          if(start < 0)
            start = 0;
          if(end > static_cast<int>(sv.size()))
            end = sv.size();
          return convert_string_literal_from_text(
            sv.substr(start, end - start));
        }
        if(method == "substring" && num_args.size() >= 1)
          return convert_string_literal_from_text(sv.substr(num_args[0]));
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
        if(method == "charAt" && !num_args.empty())
        {
          int idx = num_args[0];
          if(idx >= 0 && idx < static_cast<int>(sv.size()))
            return convert_string_literal_from_text(std::string(1, sv[idx]));
          return convert_string_literal_from_text("");
        }
        if(method == "startsWith" && !str_args.empty())
          return sv.substr(0, str_args[0].size()) == str_args[0]
                   ? exprt{true_exprt{}}
                   : exprt{false_exprt{}};
        if(method == "endsWith" && !str_args.empty())
          return sv.size() >= str_args[0].size() &&
                     sv.substr(sv.size() - str_args[0].size()) == str_args[0]
                   ? exprt{true_exprt{}}
                   : exprt{false_exprt{}};
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
          std::string pad = " ";
          if(!str_args.empty())
            pad = str_args[0];
          std::string result = sv;
          while(static_cast<int>(result.size()) < target_len)
            result = pad + result;
          return convert_string_literal_from_text(
            result.substr(result.size() - target_len));
        }
        if(method == "padEnd" && !num_args.empty())
        {
          int target_len = num_args[0];
          std::string pad = " ";
          if(!str_args.empty())
            pad = str_args[0];
          std::string result = sv;
          while(static_cast<int>(result.size()) < target_len)
            result += pad;
          return convert_string_literal_from_text(result.substr(0, target_len));
        }
        if(method == "repeat" && !num_args.empty())
        {
          int count = num_args[0];
          std::string result;
          for(int i = 0; i < count; ++i)
            result += sv;
          return convert_string_literal_from_text(result);
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
          struct_typet list_type;
          list_type.components().push_back(
            struct_typet::componentt{"length", signedbv_typet{64}});
          list_type.components().push_back(
            struct_typet::componentt{"data", arr_type});
          list_type.set_tag("typescript_array");
          return struct_exprt{
            {from_integer(actual, signedbv_typet{64}),
             array_exprt{std::move(elts), arr_type}},
            list_type};
        }
      }
      // Nondet fallback for non-constant strings
      return side_effect_expr_nondett{double_type(), get_location(node)};
    }
    // Array.map: create new array by applying callback to each element
    if(
      !obj_expr.is_nil() && obj_expr.type().id() == ID_struct &&
      to_struct_type(obj_expr.type()).get_tag() == "typescript_array" &&
      method == "map" && args.is_array() && !to_json_array(args).empty())
    {
      const jsont &callback = *to_json_array(args).begin();
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
        struct_typet list_type;
        list_type.components().push_back(
          struct_typet::componentt{"length", signedbv_typet{64}});
        list_type.components().push_back(
          struct_typet::componentt{"data", arr_type});
        list_type.set_tag("typescript_array");
        return struct_exprt{
          {from_integer(actual_len, signedbv_typet{64}),
           array_exprt{std::move(result_elts), arr_type}},
          list_type};
      }
    }
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
                  {cdata.operands()[ci]},
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
            struct_typet list_type;
            list_type.components().push_back(
              struct_typet::componentt{"length", signedbv_typet{64}});
            list_type.components().push_back(
              struct_typet::componentt{"data", arr_type});
            list_type.set_tag("typescript_array");
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
        struct_typet list_type;
        list_type.components().push_back(
          struct_typet::componentt{"length", signedbv_typet{64}});
        list_type.components().push_back(
          struct_typet::componentt{"data", arr_type});
        list_type.set_tag("typescript_array");
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
            {data.operands()[idx]},
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
            {symbol_exprt{acc_id, acc_type}, data.operands()[idx]},
            acc_type,
            source_locationt{}};
          pending_stmts.push_back(
            code_frontend_assignt{symbol_exprt{acc_id, acc_type}, call});
        }
        return symbol_exprt{acc_id, acc_type};
      }
    }
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
        struct_typet list_type;
        list_type.components().push_back(
          struct_typet::componentt{"length", signedbv_typet{64}});
        list_type.components().push_back(
          struct_typet::componentt{"data", arr_type});
        list_type.set_tag("typescript_array");
        return struct_exprt{
          {from_integer(actual, signedbv_typet{64}),
           array_exprt{std::move(result_elts), arr_type}},
          list_type};
      }
    }
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
          if(
            idx < data.operands().size() &&
            data.operands()[idx].is_constant() &&
            data.operands()[idx].type().id() == ID_floatbv)
          {
            ieee_floatt fv{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            fv.from_expr(to_constant_expr(data.operands()[idx]));
            double d = std::stod(fv.to_ansi_c_string());
            if(d == std::floor(d) && std::abs(d) < 1e15)
              result += std::to_string(static_cast<long long>(d));
            else
              result += fv.to_ansi_c_string();
          }
        }
        return convert_string_literal_from_text(result);
      }
      return side_effect_expr_nondett{
        typescript_string_type(), get_location(node)};
    }
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
        struct_typet list_type;
        list_type.components().push_back(
          struct_typet::componentt{"length", signedbv_typet{64}});
        list_type.components().push_back(
          struct_typet::componentt{"data", arr_type});
        list_type.set_tag("typescript_array");
        return struct_exprt{
          {from_integer(actual, signedbv_typet{64}),
           array_exprt{std::move(reversed), arr_type}},
          list_type};
      }
    }
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
      }
      return side_effect_expr_nondett{double_type(), get_location(node)};
    }
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
        struct_typet list_type;
        list_type.components().push_back(
          struct_typet::componentt{"length", signedbv_typet{64}});
        list_type.components().push_back(
          struct_typet::componentt{"data", arr_type});
        list_type.set_tag("typescript_array");
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
        struct_typet list_type;
        list_type.components().push_back(
          struct_typet::componentt{"length", signedbv_typet{64}});
        list_type.components().push_back(
          struct_typet::componentt{"data", arr_type});
        list_type.set_tag("typescript_array");
        return struct_exprt{
          {from_integer(actual, signedbv_typet{64}),
           array_exprt{std::move(combined), arr_type}},
          list_type};
      }
    }
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
          exprt len = member_exprt{obj_expr, "length", signedbv_typet{64}};
          exprt new_len = minus_exprt{len, from_integer(1, signedbv_typet{64})};
          exprt data =
            member_exprt{obj_expr, "data", st.get_component("data").type()};
          pending_stmts.push_back(code_frontend_assignt{len, new_len});
          return index_exprt{data, new_len};
        }
        if(method == "length")
          return member_exprt{obj_expr, "length", signedbv_typet{64}};
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
          res = std::max(arg_vals[0], arg_vals[1]);
        else if(method == "min" && arg_vals.size() >= 2)
          res = std::min(arg_vals[0], arg_vals[1]);
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
      // Try as local variable: current_function::func_name
      if(!current_function.empty())
      {
        irep_idt local_id{"typescript::" + current_function + "::" + func_name};
        const symbolt *local_sym = symbol_table.lookup(local_id);
        if(
          local_sym != nullptr && (local_sym->type.id() == ID_pointer ||
                                   local_sym->type.id() == ID_code))
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
          // Dereference if pointer
          exprt callee_expr = local_sym->symbol_expr();
          if(local_sym->type.id() == ID_pointer)
            callee_expr = dereference_exprt{callee_expr};
          return side_effect_expr_function_callt{
            callee_expr, std::move(arguments), ret_type, get_location(node)};
        }
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
      // Fill captured variable args
      if(captured_var_map.count(func_id) > 0)
      {
        for(const auto &[cv_name, cv_outer_id] : captured_var_map[func_id])
        {
          const symbolt *cv_sym = symbol_table.lookup(cv_outer_id);
          if(cv_sym != nullptr)
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
          struct_typet list_type;
          list_type.components().push_back(
            struct_typet::componentt{"length", signedbv_typet{64}});
          list_type.components().push_back(
            struct_typet::componentt{"data", arr_type});
          list_type.set_tag("typescript_array");
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

    log.warning() << "Unknown function: " << func_name << messaget::eom;
    return side_effect_expr_nondett{double_type(), get_location(node)};
  }

  return side_effect_expr_nondett{double_type(), get_location(node)};
}

// --- Statement conversion ---

bool typescript_convertert::convert()
{
  const jsont &statements = json_member(ast_json, "statements");
  convert_module_body(statements);
  return false; // success
}
