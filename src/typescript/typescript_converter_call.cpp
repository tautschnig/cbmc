/// \\file
/// TypeScript to GOTO converter — call expression and method handlers

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/floatbv_expr.h>
#include <util/ieee_float.h>
#include <util/irep.h>
#include <util/mathematical_expr.h>
#include <util/mathematical_types.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/symbol.h>

#include <goto-programs/goto_functions.h>

#include "typescript_converter.h"
#include "typescript_regex.h"
#include "typescript_types.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>

exprt typescript_convertert::convert_call_expression(const jsont &node)
{
  const jsont &callee = json_member(node, "expression");
  const jsont &args = json_member(node, "arguments");

  // ES2024 §13.3.10: Dynamic import — `import("./mod")` returns a
  // Promise<typeof module>. With our sequential async model, await
  // unwraps it to the module exports object.
  //
  // The parser annotates such calls with `dynamicImport: true` and
  // (where it could resolve the target module) `moduleExports`: a
  // list of {name, type} entries describing the exported symbols.
  //
  // We construct an anonymous struct value with one nondet field
  // per export, of approximately the right shape. This is sound
  // (over-approximates) and lets `mod.foo` reads through the
  // existing struct-property machinery return values of the
  // matching field type.
  if(json_member(node, "dynamicImport").is_true())
  {
    const jsont &exports_arr = json_member(node, "moduleExports");
    if(exports_arr.is_array() && !to_json_array(exports_arr).empty())
    {
      struct_typet mod_type;
      std::vector<exprt> field_values;
      for(const auto &exp : to_json_array(exports_arr))
      {
        std::string name = json_string(json_member(exp, "name"));
        std::string ty = json_string(json_member(exp, "type"));
        // Map TS-typeString to CBMC type. Crude mapping; more
        // sophisticated dispatch could route to the existing
        // type-string parser.
        typet field_ty;
        if(ty == "number")
          field_ty = double_type();
        else if(ty == "boolean")
          field_ty = bool_typet{};
        else if(ty == "string")
          field_ty = typescript_string_type();
        else
          field_ty = typescript_string_type(); // safe default
        mod_type.components().push_back(
          struct_typet::componentt{name, field_ty});
        field_values.push_back(
          side_effect_expr_nondett{field_ty, get_location(node)});
      }
      mod_type.set_tag("typescript_dynamic_module");
      return struct_exprt{std::move(field_values), mod_type};
    }
    // Could not resolve the target module — fall through to a
    // generic nondet object.
    log.warning() << "Dynamic import target not resolved at conversion "
                  << "time; returning empty nondet module struct"
                  << messaget::eom;
    struct_typet empty_mod;
    empty_mod.set_tag("typescript_dynamic_module");
    return side_effect_expr_nondett{empty_mod, get_location(node)};
  }

  // ES2024 §20.1.3.3: X.prototype.isPrototypeOf(y).
  // Resolve statically by checking if y's class has X in its
  // parent chain (known at conversion time from parent_class map).
  if(is_kind(callee, "PropertyAccessExpression"))
  {
    std::string method =
      json_string(json_member(json_member(callee, "name"), "text"));
    if(
      method == "isPrototypeOf" && args.is_array() &&
      !to_json_array(args).empty())
    {
      // The receiver is X.prototype — extract X from the chain.
      const jsont &recv = json_member(callee, "expression");
      if(is_kind(recv, "PropertyAccessExpression"))
      {
        std::string proto_prop =
          json_string(json_member(json_member(recv, "name"), "text"));
        if(proto_prop == "prototype")
        {
          std::string parent_cls =
            json_string(json_member(json_member(recv, "expression"), "text"));
          // Get the argument's type to determine its class.
          exprt arg_expr = convert_expression(*to_json_array(args).begin());
          if(arg_expr.type().id() == ID_struct)
          {
            std::string arg_tag =
              id2string(to_struct_type(arg_expr.type()).get_tag());
            // Strip "typescript_class_" prefix.
            if(arg_tag.find("typescript_class_") == 0)
              arg_tag = arg_tag.substr(17);
            // Walk the parent chain.
            std::string cur = arg_tag;
            bool found = false;
            for(int depth = 0; depth < 10 && !cur.empty(); depth++)
            {
              if(cur == parent_cls)
              {
                found = true;
                break;
              }
              auto pit = parent_class.find(cur);
              if(pit != parent_class.end())
                cur = pit->second;
              else
                break;
            }
            return found ? exprt{true_exprt{}} : exprt{false_exprt{}};
          }
        }
      }
    }
  }

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
    // ES2024 §25.5 JSON
    if(obj == "JSON")
    {
      // Helper: stringify a constant expression at conversion time.
      std::function<std::optional<std::string>(const exprt &)> stringify;
      stringify = [&](const exprt &e) -> std::optional<std::string>
      {
        // Resolve symbols to their stored values.
        exprt cur = e;
        if(cur.id() == ID_symbol)
        {
          const symbolt *s =
            symbol_table.lookup(to_symbol_expr(cur).get_identifier());
          if(s && !s->value.is_nil())
            cur = s->value;
        }
        // Handle unary_minus of numeric constant.
        if(
          cur.id() == ID_unary_minus && cur.operands().size() == 1 &&
          cur.operands()[0].is_constant() &&
          cur.operands()[0].type().id() == ID_floatbv)
        {
          ieee_floatt v{
            to_constant_expr(cur.operands()[0]), ieee_floatt::ROUND_TO_EVEN};
          if(v.is_NaN() || v.is_infinity())
            return std::string("null");
          // Negate and stringify
          v.negate();
          ieee_floatt rounded = v;
          rounded = rounded.round_to_integral();
          if(rounded == v)
          {
            mp_integer i = v.to_integer();
            return integer2string(i);
          }
          return v.to_ansi_c_string();
        }
        // Boolean constants
        if(cur.is_constant() && cur.type().id() == ID_bool)
          return cur.is_true() ? std::string("true") : std::string("false");
        // Numeric constants
        if(cur.is_constant() && cur.type().id() == ID_floatbv)
        {
          ieee_floatt v{to_constant_expr(cur), ieee_floatt::ROUND_TO_EVEN};
          // JSON: NaN and Infinity stringify to "null"
          if(v.is_NaN() || v.is_infinity())
            return std::string("null");
          ieee_floatt rounded = v;
          rounded = rounded.round_to_integral();
          if(rounded == v)
          {
            mp_integer i = v.to_integer();
            return integer2string(i);
          }
          return v.to_ansi_c_string();
        }
        // String constants
        if(is_typescript_string_type(cur.type()))
        {
          std::string raw = extract_string_value(cur);
          if(!raw.empty())
          {
            std::string s = raw.substr(2);
            std::string escaped = "\"";
            for(char c : s)
            {
              if(c == '"')
                escaped += "\\\"";
              else if(c == '\\')
                escaped += "\\\\";
              else if(c == '\n')
                escaped += "\\n";
              else if(c == '\t')
                escaped += "\\t";
              else
                escaped += c;
            }
            escaped += "\"";
            return escaped;
          }
          return std::nullopt;
        }
        // Array constants (typescript_array struct)
        if(
          cur.id() == ID_struct && cur.type().id() == ID_struct &&
          to_struct_type(cur.type()).get_tag() == "typescript_array")
        {
          if(cur.operands().size() < 2)
            return std::nullopt;
          if(!cur.operands()[0].is_constant())
            return std::nullopt;
          mp_integer len;
          if(to_integer(to_constant_expr(cur.operands()[0]), len))
            return std::nullopt;
          const exprt &data = cur.operands()[1];
          std::string result = "[";
          for(mp_integer i = 0; i < len; ++i)
          {
            if(i > 0)
              result += ",";
            auto idx = i.to_ulong();
            if(idx >= data.operands().size())
              return std::nullopt;
            auto elem = stringify(data.operands()[idx]);
            if(!elem.has_value())
              return std::nullopt;
            result += elem.value();
          }
          result += "]";
          return result;
        }
        // Object-literal constants (arbitrary struct that's not array)
        if(cur.id() == ID_struct && cur.type().id() == ID_struct)
        {
          const auto &st = to_struct_type(cur.type());
          std::string tag = id2string(st.get_tag());
          // Skip our internal tags
          if(
            tag == "typescript_union" || tag == "typescript_tuple" ||
            tag == "typescript_array" || is_typescript_string_type(cur.type()))
            return std::nullopt;
          std::string result = "{";
          bool first = true;
          for(std::size_t i = 0;
              i < st.components().size() && i < cur.operands().size();
              ++i)
          {
            std::string fname = id2string(st.components()[i].get_name());
            // Skip internal fields
            if(fname.substr(0, 2) == "__")
              continue;
            if(!first)
              result += ",";
            first = false;
            result += "\"" + fname + "\":";
            auto val = stringify(cur.operands()[i]);
            if(!val.has_value())
              return std::nullopt;
            result += val.value();
          }
          result += "}";
          return result;
        }
        return std::nullopt;
      };

      if(
        method == "stringify" && args.is_array() &&
        !to_json_array(args).empty())
      {
        exprt arg = convert_expression(*to_json_array(args).begin());
        if(arg.id() == ID_symbol)
        {
          const symbolt *s =
            symbol_table.lookup(to_symbol_expr(arg).get_identifier());
          if(s && !s->value.is_nil())
            arg = s->value;
        }
        // Null → "null" (null is our NaN sentinel)
        if(arg.is_constant() && arg.type().id() == ID_floatbv)
        {
          ieee_floatt v{to_constant_expr(arg), ieee_floatt::ROUND_TO_EVEN};
          if(v.is_NaN())
            return convert_string_literal_from_text("null");
        }
        auto s = stringify(arg);
        if(s.has_value())
          return convert_string_literal_from_text(s.value());
        // Symbolic number: emit a string whose length is constrained
        // to be >= 1 (every non-NaN/Infinity number produces at
        // least one digit). Content remains nondet.
        if(arg.type().id() == ID_floatbv)
        {
          struct_typet str_type = typescript_string_type();
          typet len_type = str_type.components()[0].type();
          typet data_type = str_type.components()[1].type();
          exprt data_nondet =
            side_effect_expr_nondett{data_type, source_locationt{}};
          // Create a nondet length constrained via assume.
          static unsigned sym_ctr = 0;
          std::string name = "__ts_json_str_len_" + std::to_string(sym_ctr++);
          irep_idt sym_id{"typescript::" + name};
          if(symbol_table.lookup(sym_id) == nullptr)
          {
            symbolt ls{sym_id, len_type, "typescript"};
            ls.base_name = name;
            ls.is_lvalue = true;
            ls.is_state_var = true;
            symbol_table.add(ls);
          }
          exprt len_sym = symbol_table.lookup_ref(sym_id).symbol_expr();
          pending_stmts.push_back(code_frontend_assignt{
            len_sym, side_effect_expr_nondett{len_type, source_locationt{}}});
          pending_stmts.push_back(code_assumet{and_exprt{
            binary_relation_exprt{len_sym, ID_ge, from_integer(1, len_type)},
            binary_relation_exprt{
              len_sym,
              ID_le,
              from_integer(TYPESCRIPT_MAX_STRING_LENGTH, len_type)}}});
          return struct_exprt{{len_sym, data_nondet}, str_type};
        }
        // Nondet fallback for non-constant
        return side_effect_expr_nondett{
          typescript_string_type(), get_location(node)};
      }
      if(method == "parse" && args.is_array() && !to_json_array(args).empty())
      {
        // For constant JSON string literals, parse at conversion time.
        exprt arg = convert_expression(*to_json_array(args).begin());
        std::string raw = extract_string_value(arg);
        if(!raw.empty())
        {
          std::string json_str = raw.substr(2);
          // Quick-and-dirty parser for JSON primitives (not full JSON).
          // For complex parsing, users should use a real parser.
          // Numbers:
          if(
            !json_str.empty() &&
            (std::isdigit(static_cast<unsigned char>(json_str[0])) ||
             json_str[0] == '-'))
          {
            try
            {
              double d = std::stod(json_str);
              ieee_floatt fv{
                ieee_float_spect::double_precision(),
                ieee_floatt::rounding_modet::ROUND_TO_EVEN};
              fv.from_double(d);
              return fv.to_expr();
            }
            catch(...)
            {
            }
          }
          // Booleans:
          if(json_str == "true")
            return true_exprt{};
          if(json_str == "false")
            return false_exprt{};
          // null → NaN
          if(json_str == "null")
          {
            return ts_nan_with_payload(TS_NAN_PAYLOAD_NULL);
          }
          // String:
          if(
            json_str.size() >= 2 && json_str.front() == '"' &&
            json_str.back() == '"')
          {
            std::string inner = json_str.substr(1, json_str.size() - 2);
            // Unescape basic cases
            std::string result;
            for(std::size_t i = 0; i < inner.size(); ++i)
            {
              if(inner[i] == '\\' && i + 1 < inner.size())
              {
                char c = inner[i + 1];
                if(c == 'n')
                  result += '\n';
                else if(c == 't')
                  result += '\t';
                else if(c == '"')
                  result += '"';
                else if(c == '\\')
                  result += '\\';
                else
                  result += c;
                ++i;
              }
              else
                result += inner[i];
            }
            return convert_string_literal_from_text(result);
          }
          // Array: [1,2,3]
          if(
            json_str.size() >= 2 && json_str.front() == '[' &&
            json_str.back() == ']')
          {
            std::string inner = json_str.substr(1, json_str.size() - 2);
            // Simple parser for comma-separated primitives.
            exprt::operandst elements;
            typet elem_type = double_type();
            std::size_t pos = 0;
            while(pos < inner.size())
            {
              std::size_t next = pos;
              int depth = 0;
              while(next < inner.size())
              {
                char c = inner[next];
                if(c == '[' || c == '{')
                  depth++;
                else if(c == ']' || c == '}')
                  depth--;
                else if(c == ',' && depth == 0)
                  break;
                next++;
              }
              std::string part = inner.substr(pos, next - pos);
              while(!part.empty() && part[0] == ' ')
                part.erase(0, 1);
              while(!part.empty() && part.back() == ' ')
                part.pop_back();
              if(!part.empty())
              {
                try
                {
                  double d = std::stod(part);
                  ieee_floatt fv{
                    ieee_float_spect::double_precision(),
                    ieee_floatt::rounding_modet::ROUND_TO_EVEN};
                  fv.from_double(d);
                  elements.push_back(fv.to_expr());
                }
                catch(...)
                {
                }
              }
              pos = next + 1;
            }
            std::size_t actual = elements.size();
            std::size_t max_len = TYPESCRIPT_MAX_ARRAY_LENGTH;
            while(elements.size() < max_len)
              elements.push_back(from_integer(0, elem_type));
            array_typet arr_type{
              elem_type, from_integer(max_len, signedbv_typet{64})};
            struct_typet list_type = make_array_struct_type(arr_type);
            return struct_exprt{
              {from_integer(actual, signedbv_typet{64}),
               array_exprt{std::move(elements), arr_type}},
              list_type};
          }
          // Object: {"a":1, "b":"s", ...}. Recursively parsed.
          if(
            json_str.size() >= 2 && json_str.front() == '{' &&
            json_str.back() == '}')
          {
            // Helper: parse a JSON primitive (number, string, bool,
            // null) into an expr. Returns nil on failure. No recursion
            // into nested objects/arrays here — that's handled by
            // recursively invoking this helper via convert_string +
            // tree walking. For this round we only support one-level
            // nesting via recursive call.
            std::function<exprt(const std::string &)> parse_value =
              [&](const std::string &val) -> exprt
            {
              std::string v = val;
              while(!v.empty() && v.front() == ' ')
                v.erase(0, 1);
              while(!v.empty() && v.back() == ' ')
                v.pop_back();
              if(v.empty())
                return nil_exprt{};
              // Numbers
              if(std::isdigit(static_cast<unsigned char>(v[0])) || v[0] == '-')
              {
                try
                {
                  double d = std::stod(v);
                  ieee_floatt fv{
                    ieee_float_spect::double_precision(),
                    ieee_floatt::rounding_modet::ROUND_TO_EVEN};
                  fv.from_double(d);
                  return fv.to_expr();
                }
                catch(...)
                {
                }
              }
              if(v == "true")
                return true_exprt{};
              if(v == "false")
                return false_exprt{};
              if(v == "null")
              {
                return ts_nan_with_payload(TS_NAN_PAYLOAD_NULL);
              }
              if(v.size() >= 2 && v.front() == '"' && v.back() == '"')
              {
                std::string inner = v.substr(1, v.size() - 2);
                std::string res;
                for(std::size_t i = 0; i < inner.size(); ++i)
                {
                  if(inner[i] == '\\' && i + 1 < inner.size())
                  {
                    char c = inner[i + 1];
                    if(c == 'n')
                      res += '\n';
                    else if(c == 't')
                      res += '\t';
                    else if(c == '"')
                      res += '"';
                    else if(c == '\\')
                      res += '\\';
                    else
                      res += c;
                    ++i;
                  }
                  else
                    res += inner[i];
                }
                return convert_string_literal_from_text(res);
              }
              // Recursively parse nested object/array by making a
              // synthetic JSON.parse call... simpler: inline.
              if(v.size() >= 2 && v.front() == '{' && v.back() == '}')
                return parse_value(v); // re-dispatch via another path
              return nil_exprt{};
            };
            // Tokenize by top-level commas at depth 0.
            std::string inner = json_str.substr(1, json_str.size() - 2);
            struct_typet::componentst components;
            exprt::operandst fields;
            std::size_t pos = 0;
            auto skip_ws = [&]()
            {
              while(pos < inner.size() &&
                    (inner[pos] == ' ' || inner[pos] == '\t'))
                pos++;
            };
            while(pos < inner.size())
            {
              skip_ws();
              if(pos >= inner.size())
                break;
              if(inner[pos] != '"')
                break; // malformed
              // Parse key
              std::size_t key_end = pos + 1;
              while(key_end < inner.size() && inner[key_end] != '"')
                key_end++;
              std::string key = inner.substr(pos + 1, key_end - pos - 1);
              pos = key_end + 1;
              skip_ws();
              if(pos >= inner.size() || inner[pos] != ':')
                break;
              pos++;
              // Find value end (next top-level comma or end).
              std::size_t val_start = pos;
              int depth = 0;
              bool in_str = false;
              while(pos < inner.size())
              {
                char c = inner[pos];
                if(c == '"' && (pos == 0 || inner[pos - 1] != '\\'))
                  in_str = !in_str;
                else if(!in_str && (c == '{' || c == '['))
                  depth++;
                else if(!in_str && (c == '}' || c == ']'))
                  depth--;
                else if(!in_str && c == ',' && depth == 0)
                  break;
                pos++;
              }
              std::string val = inner.substr(val_start, pos - val_start);
              // Handle nested object/array recursively by in-place parse
              std::string v_trimmed = val;
              while(!v_trimmed.empty() && v_trimmed.front() == ' ')
                v_trimmed.erase(0, 1);
              while(!v_trimmed.empty() && v_trimmed.back() == ' ')
                v_trimmed.pop_back();
              exprt ve;
              if(
                v_trimmed.size() >= 2 && v_trimmed.front() == '{' &&
                v_trimmed.back() == '}')
              {
                // Recurse via a synthetic call: build a string literal
                // and recurse through JSON.parse. Simpler: construct the
                // sub-object directly. We cannot call ourselves cleanly,
                // so delegate to a lambda that re-runs the object body.
                // For one level of nesting this simple recursion suffices.
                jsont nested_call;
                ve = nil_exprt{}; // placeholder
                // Synthesize: parse the nested object by invoking the
                // same logic. To avoid code duplication, call
                // convert_expression on a newly-constructed AST node —
                // but that's heavy. Instead, just handle one level of
                // nesting inline: re-use parse_value for primitives,
                // and recursion only works for non-object values here.
                // For nested objects, we emit a struct with the parsed
                // key-value pairs by doing another round of parsing on
                // the sub-string.
                struct_typet::componentst sub_components;
                exprt::operandst sub_fields;
                std::string sub_inner =
                  v_trimmed.substr(1, v_trimmed.size() - 2);
                std::size_t sp = 0;
                while(sp < sub_inner.size())
                {
                  while(sp < sub_inner.size() &&
                        (sub_inner[sp] == ' ' || sub_inner[sp] == '\t'))
                    sp++;
                  if(sp >= sub_inner.size() || sub_inner[sp] != '"')
                    break;
                  std::size_t ske = sp + 1;
                  while(ske < sub_inner.size() && sub_inner[ske] != '"')
                    ske++;
                  std::string sk = sub_inner.substr(sp + 1, ske - sp - 1);
                  sp = ske + 1;
                  while(sp < sub_inner.size() && sub_inner[sp] == ' ')
                    sp++;
                  if(sp >= sub_inner.size() || sub_inner[sp] != ':')
                    break;
                  sp++;
                  std::size_t sv_start = sp;
                  int sd = 0;
                  while(sp < sub_inner.size())
                  {
                    char c = sub_inner[sp];
                    if(c == '{' || c == '[')
                      sd++;
                    else if(c == '}' || c == ']')
                      sd--;
                    else if(c == ',' && sd == 0)
                      break;
                    sp++;
                  }
                  std::string sv = sub_inner.substr(sv_start, sp - sv_start);
                  exprt sve = parse_value(sv);
                  if(!sve.is_nil())
                  {
                    sub_components.push_back(
                      struct_typet::componentt{sk, sve.type()});
                    sub_fields.push_back(sve);
                  }
                  if(sp < sub_inner.size() && sub_inner[sp] == ',')
                    sp++;
                }
                ve = struct_exprt{sub_fields, struct_typet{sub_components}};
              }
              else
              {
                ve = parse_value(val);
              }
              if(!ve.is_nil())
              {
                components.push_back(struct_typet::componentt{key, ve.type()});
                fields.push_back(ve);
              }
              if(pos < inner.size() && inner[pos] == ',')
                pos++;
            }
            return struct_exprt{fields, struct_typet{components}};
          }
        }
        // Non-constant or unparseable: nondet
        return side_effect_expr_nondett{double_type(), get_location(node)};
      }
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
      // ES2024 §20.1.2.5: Object.entries(o) returns [[k1,v1], ...]
      if(method == "entries" && !call_args.empty())
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
          // Build an array where each element is a tuple-struct [key,value].
          // For simplicity, we use a flat array: Object.entries works in
          // JS but deep asserts on nested tuples are impractical without
          // more machinery.
          exprt::operandst elts;
          for(std::size_t i = 0;
              i < st.components().size() && i < src.operands().size();
              ++i)
          {
            // Each entry is a 2-tuple: [key, value]
            struct_typet tuple_st;
            tuple_st.components().push_back(
              struct_typet::componentt{"_0", typescript_string_type()});
            tuple_st.components().push_back(
              struct_typet::componentt{"_1", src.operands()[i].type()});
            tuple_st.set_tag("typescript_tuple");
            elts.push_back(struct_exprt{
              {convert_string_literal_from_text(
                 id2string(st.components()[i].get_name())),
               src.operands()[i]},
              tuple_st});
          }
          std::size_t actual = elts.size();
          // Use nondet type for mixed elements; result is array-of-tuples.
          typet elem_type = actual > 0 ? elts[0].type() : double_type();
          std::size_t max_len = TYPESCRIPT_MAX_ARRAY_LENGTH;
          while(elts.size() < max_len)
            elts.push_back(
              side_effect_expr_nondett{elem_type, source_locationt{}});
          array_typet arr_type{
            elem_type, from_integer(max_len, signedbv_typet{64})};
          struct_typet list_type = make_array_struct_type(arr_type);
          return struct_exprt{
            {from_integer(actual, signedbv_typet{64}),
             array_exprt{std::move(elts), arr_type}},
            list_type};
        }
      }
      // ES2024 §20.1.2.11: Object.is — SameValue comparison.
      //   - NaN is same as NaN
      //   - +0 is NOT same as -0
      //   - Otherwise: same as ===
      if(method == "is" && call_args.size() >= 2)
      {
        exprt left = call_args[0];
        exprt right = call_args[1];
        // If both are NaN constants: return true (explicit override)
        auto is_nan_const = [](const exprt &e)
        {
          if(!e.is_constant() || e.type().id() != ID_floatbv)
            return false;
          ieee_floatt v{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          v.from_expr(to_constant_expr(e));
          return v.is_NaN();
        };
        if(is_nan_const(left) && is_nan_const(right))
          return true_exprt{};
        // Otherwise: strict equality behavior. Types must match and
        // values must be equal. For floats, NaN != NaN by IEEE-754;
        // here we've already handled both-NaN above.
        if(left.type() != right.type())
          return false_exprt{};
        if(left.type().id() == ID_floatbv)
        {
          // NaN on only one side → false
          if(is_nan_const(left) || is_nan_const(right))
            return false_exprt{};
          // Signed-zero distinction: +0 and -0 are NOT the same value
          // per ES2024 §7.2.11 SameValue. IEEE float_equal considers
          // them equal, so we detect the case explicitly when both
          // sides are constant-zero and differ in sign.
          auto is_zero_const = [](const exprt &e) -> std::optional<bool>
          {
            if(!e.is_constant() || e.type().id() != ID_floatbv)
              return std::nullopt;
            ieee_floatt v{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            v.from_expr(to_constant_expr(e));
            if(!v.is_zero())
              return std::nullopt;
            return v.get_sign(); // true = negative zero
          };
          auto sign_minus = [](const exprt &e) -> bool
          { return e.id() == ID_unary_minus; };
          // Handle constant -0 that's stored as unary_minus{0}.
          auto ls = is_zero_const(left);
          if(!ls.has_value() && sign_minus(left))
            ls = is_zero_const(left.operands()[0]).has_value() &&
                     !is_zero_const(left.operands()[0]).value()
                   ? std::optional<bool>(true)
                   : std::nullopt;
          auto rs = is_zero_const(right);
          if(!rs.has_value() && sign_minus(right))
            rs = is_zero_const(right.operands()[0]).has_value() &&
                     !is_zero_const(right.operands()[0]).value()
                   ? std::optional<bool>(true)
                   : std::nullopt;
          if(ls.has_value() && rs.has_value() && ls.value() != rs.value())
            return false_exprt{};
          // Both non-NaN non-opposite-sign-zero symbolic: IEEE equal.
          return ieee_float_equal_exprt{left, right};
        }
        if(is_typescript_string_type(left.type()))
        {
          std::string ls = extract_string_value(left);
          std::string rs = extract_string_value(right);
          if(!ls.empty() && !rs.empty())
            return ls == rs ? exprt{true_exprt{}} : exprt{false_exprt{}};
        }
        return equal_exprt{left, right};
      }
      // ES2024 §20.1.2.1: Object.assign(target, ...sources) — merge.
      // Returns target with sources' own enumerable properties assigned.
      if(method == "assign" && call_args.size() >= 1)
      {
        // Build a merged struct from all sources, with later overriding earlier.
        // All args must be struct-typed.
        struct_typet::componentst merged_components;
        exprt::operandst merged_values;
        for(const auto &src : call_args)
        {
          exprt resolved = src;
          if(resolved.id() == ID_symbol)
          {
            const symbolt *s =
              symbol_table.lookup(to_symbol_expr(resolved).get_identifier());
            if(s && !s->value.is_nil())
              resolved = s->value;
          }
          if(resolved.type().id() != ID_struct || resolved.id() != ID_struct)
            continue;
          const auto &st = to_struct_type(resolved.type());
          for(std::size_t i = 0;
              i < st.components().size() && i < resolved.operands().size();
              ++i)
          {
            std::string name = id2string(st.components()[i].get_name());
            // Override existing
            bool overridden = false;
            for(std::size_t j = 0; j < merged_components.size(); ++j)
            {
              if(id2string(merged_components[j].get_name()) == name)
              {
                merged_values[j] = resolved.operands()[i];
                overridden = true;
                break;
              }
            }
            if(!overridden)
            {
              merged_components.push_back(st.components()[i]);
              merged_values.push_back(resolved.operands()[i]);
            }
          }
        }
        return struct_exprt{merged_values, struct_typet{merged_components}};
      }
      // ES2024 §20.1.2.7: Object.freeze(o) — prevents property writes.
      // Returns the argument. We record the symbol name so isFrozen and
      // assignment conversion can check frozenness.
      if(method == "freeze" && !call_args.empty())
      {
        exprt src = call_args[0];
        if(src.id() == ID_symbol)
          frozen_symbols.insert(to_symbol_expr(src).get_identifier());
        return src;
      }
      // ES2024 §20.1.2.15: Object.isFrozen(o)
      if(method == "isFrozen" && !call_args.empty())
      {
        exprt src = call_args[0];
        if(src.id() == ID_symbol)
        {
          bool is_frozen =
            frozen_symbols.count(to_symbol_expr(src).get_identifier()) > 0;
          return is_frozen ? exprt{true_exprt{}} : exprt{false_exprt{}};
        }
        // Non-symbol argument: conservatively return false (unfrozen).
        return false_exprt{};
      }
      // ES2024 §20.1.2.6: Object.fromEntries — iterable of [key, value]
      // pairs → object.
      if(method == "fromEntries" && !call_args.empty())
      {
        exprt arg = call_args[0];
        if(arg.id() == ID_symbol)
        {
          const symbolt *s =
            symbol_table.lookup(to_symbol_expr(arg).get_identifier());
          if(s && !s->value.is_nil())
            arg = s->value;
        }
        if(
          arg.id() == ID_struct && arg.type().id() == ID_struct &&
          to_struct_type(arg.type()).get_tag() == "typescript_array")
        {
          mp_integer len;
          if(
            arg.operands()[0].is_constant() &&
            !to_integer(to_constant_expr(arg.operands()[0]), len))
          {
            const exprt &data = arg.operands()[1];
            struct_typet::componentst components;
            exprt::operandst fields;
            for(mp_integer i = 0; i < len; ++i)
            {
              auto idx = i.to_ulong();
              if(idx >= data.operands().size())
                continue;
              const exprt &pair = data.operands()[idx];
              // Pair should be a 2-tuple struct [key, value]
              if(pair.id() != ID_struct || pair.operands().size() < 2)
                continue;
              std::string key_raw = extract_string_value(pair.operands()[0]);
              if(key_raw.empty())
                continue;
              std::string key = key_raw.substr(2);
              components.push_back(
                struct_typet::componentt{key, pair.operands()[1].type()});
              fields.push_back(pair.operands()[1]);
            }
            return struct_exprt{fields, struct_typet{components}};
          }
        }
        return side_effect_expr_nondett{double_type(), get_location(node)};
      }
      // ES2024 §20.1.2.8: Object.hasOwn(o, "key") — true iff key is
      // a direct (not inherited) property of o.
      if(method == "hasOwn" && call_args.size() >= 2)
      {
        exprt src = call_args[0];
        if(src.id() == ID_symbol)
        {
          const symbolt *s =
            symbol_table.lookup(to_symbol_expr(src).get_identifier());
          if(s && !s->value.is_nil())
            src = s->value;
        }
        std::string key = extract_string_value(call_args[1]);
        if(!key.empty() && src.type().id() == ID_struct)
        {
          std::string key_name = key.substr(2);
          const auto &st = to_struct_type(src.type());
          for(const auto &c : st.components())
          {
            if(id2string(c.get_name()) == key_name)
              return true_exprt{};
          }
          return false_exprt{};
        }
        return side_effect_expr_nondett{bool_typet{}, get_location(node)};
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
    // ES2024 §22.1.2.1: String.fromCharCode(...codes) builds a string
    // from 16-bit UTF-16 code units.
    if(obj == "String" && method == "fromCharCode" && args.is_array())
    {
      exprt::operandst chars;
      for(const auto &a : to_json_array(args))
      {
        exprt v = convert_expression(a);
        if(v.id() == ID_symbol)
        {
          const symbolt *s =
            symbol_table.lookup(to_symbol_expr(v).get_identifier());
          if(s && !s->value.is_nil())
            v = s->value;
        }
        if(v.is_constant() && v.type().id() == ID_floatbv)
        {
          ieee_floatt fv{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          fv.from_expr(to_constant_expr(v));
          int code = static_cast<int>(std::stod(fv.to_ansi_c_string()));
          chars.push_back(from_integer(code, unsignedbv_typet{16}));
        }
        else
          chars.push_back(
            side_effect_expr_nondett{unsignedbv_typet{16}, source_locationt{}});
      }
      std::size_t actual = chars.size();
      while(chars.size() < TYPESCRIPT_MAX_STRING_LENGTH)
        chars.push_back(from_integer(0, unsignedbv_typet{16}));
      struct_typet str_type = typescript_string_type();
      const auto &data_type = to_array_type(str_type.components()[1].type());
      return struct_exprt{
        {from_integer(actual, signedbv_typet{32}),
         array_exprt{std::move(chars), data_type}},
        str_type};
    }
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
    // ES2024 sec-array.of: Array.of(...items) creates an array of items.
    if(obj == "Array" && method == "of" && args.is_array())
    {
      exprt::operandst elts;
      typet elem_type = double_type();
      for(const auto &a : to_json_array(args))
      {
        exprt v = convert_expression(a);
        if(elts.empty())
          elem_type = v.type();
        elts.push_back(v);
      }
      std::size_t actual = elts.size();
      std::size_t max_len = TYPESCRIPT_MAX_ARRAY_LENGTH;
      while(elts.size() < max_len)
        elts.push_back(from_integer(0, elem_type));
      array_typet arr_type{
        elem_type, from_integer(max_len, signedbv_typet{64})};
      struct_typet list_type = make_array_struct_type(arr_type);
      return struct_exprt{
        {from_integer(actual, signedbv_typet{64}),
         array_exprt{std::move(elts), arr_type}},
        list_type};
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

      // Helper: extract double value from constant or unary-minus of
      // constant. Returns {ok, value}.
      auto extract_double = [](const exprt &e) -> std::pair<bool, double>
      {
        if(e.is_constant() && e.type().id() == ID_floatbv)
        {
          ieee_floatt fv{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          fv.from_expr(to_constant_expr(e));
          return {true, std::stod(fv.to_ansi_c_string())};
        }
        if(
          e.id() == ID_unary_minus && !e.operands().empty() &&
          e.operands()[0].is_constant() &&
          e.operands()[0].type().id() == ID_floatbv)
        {
          ieee_floatt fv{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          fv.from_expr(to_constant_expr(e.operands()[0]));
          return {true, -std::stod(fv.to_ansi_c_string())};
        }
        return {false, 0.0};
      };

      if(method == "isInteger" && !call_args.empty())
      {
        // ES2024 §21.1.2.3: Number.isInteger(x) returns true iff x is
        // a finite integer-valued number. Non-numeric args → false.
        // In --ts-integer-mode, numbers are signedbv — always integer.
        if(call_args[0].type().id() == ID_signedbv)
          return true_exprt{};
        if(call_args[0].type().id() != ID_floatbv)
          return false_exprt{};
        auto [ok, d] = extract_double(call_args[0]);
        if(ok)
        {
          if(std::isnan(d) || std::isinf(d))
            return false_exprt{};
          return d == std::floor(d) ? exprt{true_exprt{}}
                                    : exprt{false_exprt{}};
        }
        // Symbolic fallback using CBMC's floatbv_round_to_integral_exprt
        // (same primitive the C frontend uses for floor/ceil/trunc):
        //   !isnan(x) && !isinf(x) && round_to_integral(x, TOWARDZERO) == x
        exprt x = call_args[0];
        exprt round_trip = floatbv_round_to_integral_exprt{
          x, from_integer(3, signedbv_typet{32})}; // FE_TOWARDZERO
        return and_exprt{
          not_exprt{isnan_exprt{x}},
          and_exprt{
            not_exprt{isinf_exprt{x}}, ieee_float_equal_exprt{x, round_trip}}};
      }
      if(method == "isSafeInteger" && !call_args.empty())
      {
        // ES2024 §21.1.2.5: isInteger(x) && |x| <= 2^53 - 1.
        // In --ts-integer-mode, numbers are already int64 — all within range.
        if(call_args[0].type().id() == ID_signedbv)
          return true_exprt{};
        if(call_args[0].type().id() != ID_floatbv)
          return false_exprt{};
        auto [ok, d] = extract_double(call_args[0]);
        if(ok)
        {
          if(std::isnan(d) || std::isinf(d))
            return false_exprt{};
          if(d != std::floor(d))
            return false_exprt{};
          const double max_safe = 9007199254740991.0;
          return (std::fabs(d) <= max_safe) ? exprt{true_exprt{}}
                                            : exprt{false_exprt{}};
        }
        // Symbolic fallback: isInteger(x) && |x| <= 2^53 - 1.
        exprt x = call_args[0];
        exprt round_trip = floatbv_round_to_integral_exprt{
          x, from_integer(3, signedbv_typet{32})}; // FE_TOWARDZERO
        ieee_floatt max_safe{
          ieee_float_spect::double_precision(),
          ieee_floatt::rounding_modet::ROUND_TO_EVEN};
        max_safe.from_double(9007199254740991.0);
        ieee_floatt min_safe{
          ieee_float_spect::double_precision(),
          ieee_floatt::rounding_modet::ROUND_TO_EVEN};
        min_safe.from_double(-9007199254740991.0);
        return and_exprt{
          not_exprt{isnan_exprt{x}},
          and_exprt{
            not_exprt{isinf_exprt{x}},
            and_exprt{
              ieee_float_equal_exprt{x, round_trip},
              and_exprt{
                binary_relation_exprt{x, ID_ge, min_safe.to_expr()},
                binary_relation_exprt{x, ID_le, max_safe.to_expr()}}}}};
      }
      if(method == "isNaN" && !call_args.empty())
      {
        // ES2024 §21.1.2.4: Number.isNaN returns true only for the
        // real NaN value (not for null/undefined sentinels which are
        // also NaN at the IEEE level but have distinct payloads).
        if(call_args[0].type().id() != ID_floatbv)
          return false_exprt{};
        // Check: x is NaN AND x is not a null/undefined sentinel.
        // Simplest: x == canonical_NaN (bit-equal to payload 0).
        return equal_exprt{
          call_args[0], ts_nan_with_payload(TS_NAN_PAYLOAD_REAL)};
      }
      if(method == "isFinite" && !call_args.empty())
      {
        // ES2024 §21.1.2.2: Number.isFinite also returns false for
        // non-numbers.
        if(call_args[0].type().id() != ID_floatbv)
          return false_exprt{};
        return not_exprt{
          or_exprt{isnan_exprt{call_args[0]}, isinf_exprt{call_args[0]}}};
      }
      // ES2024 §21.1.2.13: Number.parseInt (string → integer).
      // §21.1.2.12: Number.parseFloat (string → number).
      if((method == "parseInt" || method == "parseFloat") && !call_args.empty())
      {
        std::string sv_raw = extract_string_value(call_args[0]);
        if(!sv_raw.empty())
        {
          std::string sv = sv_raw.substr(2);
          int radix = 10;
          if(method == "parseInt" && call_args.size() >= 2)
          {
            auto [ok, r] = extract_double(call_args[1]);
            if(ok)
              radix = static_cast<int>(r);
          }
          try
          {
            double d = method == "parseInt"
                         ? static_cast<double>(std::stoll(sv, nullptr, radix))
                         : std::stod(sv);
            ieee_floatt fv{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            fv.from_double(d);
            return fv.to_expr();
          }
          catch(...)
          {
            return ts_nan_with_payload(TS_NAN_PAYLOAD_REAL);
          }
        }
        return side_effect_expr_nondett{double_type(), get_location(node)};
      }
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
    // ES2024 §21.1.3: Number instance methods (toFixed, toString, etc.)
    if(
      !obj_expr.is_nil() && obj_expr.type().id() == ID_floatbv &&
      !is_typescript_string_type(obj_expr.type()))
    {
      // Extract constant value (including unary-minus and symbol lookup).
      auto extract_num = [this](exprt e) -> std::pair<bool, double>
      {
        if(e.id() == ID_symbol)
        {
          const symbolt *s =
            symbol_table.lookup(to_symbol_expr(e).get_identifier());
          if(s && !s->value.is_nil())
            e = s->value;
        }
        if(e.is_constant() && e.type().id() == ID_floatbv)
        {
          ieee_floatt fv{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          fv.from_expr(to_constant_expr(e));
          return {true, std::stod(fv.to_ansi_c_string())};
        }
        if(
          e.id() == ID_unary_minus && !e.operands().empty() &&
          e.operands()[0].is_constant() &&
          e.operands()[0].type().id() == ID_floatbv)
        {
          ieee_floatt fv{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          fv.from_expr(to_constant_expr(e.operands()[0]));
          return {true, -std::stod(fv.to_ansi_c_string())};
        }
        return {false, 0.0};
      };
      auto [ok, d] = extract_num(obj_expr);
      if(ok)
      {
        if(method == "toFixed")
        {
          // ES2024 §21.1.3.3: toFixed(fractionDigits).
          int digits = 0;
          if(args.is_array() && !to_json_array(args).empty())
          {
            auto [ok2, d2] =
              extract_num(convert_expression(*to_json_array(args).begin()));
            if(ok2)
              digits = static_cast<int>(d2);
          }
          char buf[64];
          std::snprintf(buf, sizeof(buf), "%.*f", digits, d);
          return convert_string_literal_from_text(std::string(buf));
        }
        if(method == "toString")
        {
          // ES2024 §21.1.3.6: toString(radix).
          int radix = 10;
          if(args.is_array() && !to_json_array(args).empty())
          {
            auto [ok2, d2] =
              extract_num(convert_expression(*to_json_array(args).begin()));
            if(ok2)
              radix = static_cast<int>(d2);
          }
          if(radix == 10)
          {
            // Use JS-like rendering: integer values without decimal.
            std::string s;
            if(d == std::floor(d) && !std::isinf(d) && !std::isnan(d))
              s = integer2string(mp_integer{static_cast<long long>(d)});
            else
            {
              char buf[64];
              std::snprintf(buf, sizeof(buf), "%g", d);
              s = buf;
            }
            return convert_string_literal_from_text(s);
          }
          // Non-base-10: integer-only via snprintf.
          if(d == std::floor(d) && !std::isinf(d) && !std::isnan(d))
          {
            long long n = static_cast<long long>(d);
            std::string s;
            bool negative = n < 0;
            if(negative)
              n = -n;
            if(n == 0)
              s = "0";
            while(n > 0)
            {
              int digit = n % radix;
              s =
                static_cast<char>(digit < 10 ? '0' + digit : 'a' + digit - 10) +
                s;
              n /= radix;
            }
            if(negative)
              s = "-" + s;
            return convert_string_literal_from_text(s);
          }
        }
      }
    }
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
          // Resolve symbol to its stored constant value if possible.
          // This lets e.g. `const k = 3; s.substring(0, k)` take the
          // constant path, matching the behaviour for `s.substring(0, 3)`.
          if(av.id() == ID_symbol)
          {
            const symbolt *s =
              symbol_table.lookup(to_symbol_expr(av).get_identifier());
            if(s && !s->value.is_nil())
              av = s->value;
          }
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
          // Fall through to the symbolic-needle handler if the first
          // string arg is empty AND came from a symbolic expression
          // (not an empty literal). We detect this by re-converting
          // arg[0] and checking extract_string_value.
          bool needle_is_symbolic = false;
          if(args.is_array() && !to_json_array(args).empty())
          {
            exprt first_arg = convert_expression(*to_json_array(args).begin());
            if(is_typescript_string_type(first_arg.type()))
            {
              std::string raw = extract_string_value(first_arg);
              if(raw.empty())
                needle_is_symbolic = true;
            }
          }
          if(!needle_is_symbolic)
          {
            // If there's a second argument (fromIndex) and it's NOT
            // in num_args (i.e. it's non-constant), fall through to
            // the solver path. We detect this by checking if the
            // call has 2+ args but num_args is empty.
            bool has_nonconst_from = false;
            if(
              args.is_array() && to_json_array(args).size() >= 2 &&
              num_args.empty())
              has_nonconst_from = true;
            if(!has_nonconst_from)
            {
              std::string needle = str_args.empty() ? "" : str_args[0];
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
              int result =
                (pos == std::string::npos) ? -1 : static_cast<int>(pos);
              uint64_t bits;
              double dv = static_cast<double>(result);
              std::memcpy(&bits, &dv, sizeof(bits));
              return constant_exprt{
                integer2bvrep(mp_integer{std::to_string(bits).c_str()}, 64),
                double_type()};
            }
          }
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
        // ES2024 §22.1.3.2: String.prototype.charCodeAt returns the
        // UTF-16 code unit at the given index, or NaN if out of range.
        if(method == "charCodeAt" && !num_args.empty())
        {
          int idx = num_args[0];
          if(idx >= 0 && idx < static_cast<int>(sv.size()))
          {
            double v = static_cast<double>(static_cast<unsigned char>(sv[idx]));
            ieee_floatt fv{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            fv.from_double(v);
            return fv.to_expr();
          }
          // Out of range: NaN
          return ts_nan_with_payload(TS_NAN_PAYLOAD_REAL);
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
          // Only constant-fold if we have at least one constant arg.
          // If num_args is empty (args are computed expressions like
          // indexOf() + 1), fall through to the solver path below.
          if(!num_args.empty())
          {
            int start_idx = num_args[0];
            int end_idx = static_cast<int>(sv.size());
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
        // ES2024 §22.1.3.17/18: padStart/padEnd with symbolic target
        // length. Emit length = max(src.length, n) directly; content
        // is nondet.
        if(
          (method == "padStart" || method == "padEnd") && num_args.empty() &&
          args.is_array() && !to_json_array(args).empty())
        {
          exprt target_arg = convert_expression(*to_json_array(args).begin());
          typet len_type =
            to_struct_type(typescript_string_type()).components()[0].type();
          exprt src_len_f;
          {
            ieee_floatt lf{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            lf.from_double(static_cast<double>(sv.size()));
            src_len_f = lf.to_expr();
          }
          // length = n if n > src_len else src_len (IEEE compare).
          exprt cond = binary_relation_exprt{target_arg, ID_gt, src_len_f};
          exprt result_len_f = if_exprt{cond, target_arg, src_len_f};
          exprt result_len = typecast_exprt{std::move(result_len_f), len_type};
          typet data_type =
            to_struct_type(typescript_string_type()).components()[1].type();
          exprt data_nondet =
            side_effect_expr_nondett{data_type, source_locationt{}};
          return struct_exprt{
            {result_len, data_nondet}, typescript_string_type()};
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
        // ES2024 §22.1.3.14: repeat with symbolic count n.
        // For a constant source string of length L and symbolic n in
        // [0, K], emit a result whose `length` field is L * n.
        // The data content is left nondet — callers that only
        // inspect .length get a precise answer; callers that index
        // into .data get nondet. This matches what a symbolic model
        // can express without the refined string solver.
        if(
          method == "repeat" && num_args.empty() && args.is_array() &&
          !to_json_array(args).empty())
        {
          exprt count_arg = convert_expression(*to_json_array(args).begin());
          // Width of our string length field:
          typet len_type =
            to_struct_type(typescript_string_type()).components()[0].type();
          // Perform the multiplication in IEEE float (matching how
          // TS numbers work), then cast to len_type.
          exprt src_len_f =
            ieee_floatt::zero(ieee_float_spect::double_precision()).to_expr();
          {
            ieee_floatt lf{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            lf.from_double(static_cast<double>(sv.size()));
            src_len_f = lf.to_expr();
          }
          exprt rm =
            symbol_exprt{"__CPROVER_rounding_mode", signedbv_typet{32}};
          ieee_float_op_exprt result_len_f{
            src_len_f, ID_floatbv_mult, count_arg, rm};
          result_len_f.type() = double_type();
          exprt result_len = typecast_exprt{std::move(result_len_f), len_type};
          typet data_type =
            to_struct_type(typescript_string_type()).components()[1].type();
          exprt data_nondet =
            side_effect_expr_nondett{data_type, source_locationt{}};
          return struct_exprt{
            {result_len, data_nondet}, typescript_string_type()};
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
      // ES2024 §22.1.3.25-31: Symbolic toLowerCase / toUpperCase /
      // ES2024 §22.1.3.25-31: Symbolic toLowerCase / toUpperCase /
      // trim via the refined-string solver. Uses
      // ts_call_string_returning_function.
      if(
        !obj_expr.is_nil() && is_typescript_string_type(obj_expr.type()) &&
        (method == "toLowerCase" || method == "toUpperCase" ||
         method == "trim" || method == "trimStart" || method == "trimEnd") &&
        (!args.is_array() || to_json_array(args).empty()))
      {
        irep_idt func_id;
        if(method == "toLowerCase")
          func_id = ID_cprover_string_to_lower_case_func;
        else if(method == "toUpperCase")
          func_id = ID_cprover_string_to_upper_case_func;
        else
          func_id = ID_cprover_string_trim_func;
        exprt refined_self = ts_string_to_refined(obj_expr);
        return ts_call_string_returning_function(func_id, {refined_self});
      }
      // ES2024 sec-string.prototype.indexof
      // indexOf on non-constant strings with constant search target:
      // scan data array for matching substring
      if(method == "indexOf" && args.is_array() && !to_json_array(args).empty())
      {
        const auto &arg_arr = to_json_array(args);
        auto ait = arg_arr.begin();
        exprt arg = convert_expression(*ait);
        ++ait;
        // Check for fromIndex (second argument).
        exprt from_index_expr = nil_exprt{};
        if(ait != arg_arr.end())
          from_index_expr = convert_expression(*ait);
        // If fromIndex is non-constant OR needle is non-constant,
        // route through the solver's cprover_string_index_of_func.
        bool has_nonconst_from =
          from_index_expr.is_not_nil() && !from_index_expr.is_constant();
        std::string search_sv = extract_string_value(arg);
        if(has_nonconst_from || search_sv.empty())
        {
          // Solver path: cprover_string_index_of_func(str, needle, from)
          exprt refined_str = ts_string_to_refined(obj_expr);
          refined_string_typet refined_ty =
            to_refined_string_type(refined_str.type());
          typet idx_type = signedbv_typet{64};
          irep_idt func_id = ID_cprover_string_index_of_func;
          if(symbol_table.lookup(func_id) == nullptr)
          {
            std::vector<typet> arg_types = {refined_ty, refined_ty, idx_type};
            mathematical_function_typet ft(std::move(arg_types), idx_type);
            symbolt fs{func_id, ft, "typescript"};
            fs.base_name = id2string(func_id);
            symbol_table.add(fs);
          }
          exprt refined_needle = is_typescript_string_type(arg.type())
                                   ? ts_string_to_refined(arg)
                                   : arg;
          exprt from_idx = from_index_expr.is_not_nil()
                             ? (from_index_expr.type().id() == ID_floatbv
                                  ? typecast_exprt{from_index_expr, idx_type}
                                  : from_index_expr)
                             : from_integer(0, idx_type);
          if(from_idx.type() != idx_type)
            from_idx = typecast_exprt{from_idx, idx_type};
          function_application_exprt app(
            symbol_exprt{func_id, symbol_table.lookup_ref(func_id).type},
            {refined_str, refined_needle, from_idx});
          app.type() = idx_type;
          return typecast_exprt{app, double_type()};
        }
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
        // Symbolic needle: build a per-position match using the needle
        // struct's fields directly. For each candidate position p in
        // [0, max_scan), build a match predicate:
        //   p + needle.length <= s.length  AND
        //   forall i in [0, max_needle): i >= needle.length OR
        //     s.data[p+i] == needle.data[i]
        if(
          is_typescript_string_type(arg.type()) &&
          is_typescript_string_type(obj_expr.type()))
        {
          struct_typet str_type = typescript_string_type();
          const auto &data_type =
            to_array_type(str_type.components()[1].type());
          exprt s_data = member_exprt{obj_expr, "data", data_type};
          exprt s_len = member_exprt{obj_expr, "length", signedbv_typet{32}};
          exprt n_data = member_exprt{arg, "data", data_type};
          exprt n_len = member_exprt{arg, "length", signedbv_typet{32}};
          std::size_t max_scan =
            std::min<std::size_t>(TYPESCRIPT_MAX_STRING_LENGTH, 16);
          std::size_t max_needle =
            std::min<std::size_t>(TYPESCRIPT_MAX_STRING_LENGTH, 8);
          exprt result = from_integer(-1, signedbv_typet{64});
          for(int p = static_cast<int>(max_scan) - 1; p >= 0; p--)
          {
            exprt matches = true_exprt{};
            for(std::size_t j = 0; j < max_needle; ++j)
            {
              exprt j_expr = from_integer(j, signedbv_typet{32});
              exprt i_lt_nlen = binary_relation_exprt{j_expr, ID_lt, n_len};
              exprt s_idx = from_integer(p + j, signedbv_typet{64});
              exprt s_char = index_exprt{s_data, s_idx};
              exprt n_char =
                index_exprt{n_data, from_integer(j, signedbv_typet{64})};
              // If i < nlen, chars must match; otherwise skip.
              exprt char_match =
                or_exprt{not_exprt{i_lt_nlen}, equal_exprt{s_char, n_char}};
              matches = and_exprt{matches, char_match};
            }
            // Bounds: p + nlen <= s.length
            exprt p_plus_nlen =
              plus_exprt{from_integer(p, signedbv_typet{32}), n_len};
            exprt in_bounds = binary_relation_exprt{p_plus_nlen, ID_le, s_len};
            exprt cond = and_exprt{matches, in_bounds};
            result =
              if_exprt{cond, from_integer(p, signedbv_typet{64}), result};
          }
          return typecast_exprt{result, double_type()};
        }
      }
      // ES2024 §22.1.3.7: String.prototype.includes — symbolic path.
      // ES2024 §22.2.5.13: RegExp.prototype.test(string).
      // Phase 1: for literal patterns (no metacharacters), .test(s)
      // is equivalent to s.includes(pattern). The receiver is our
      // string-modelled RegExp (the pattern text); the argument is
      // the string to search.
      if(
        method == "test" && args.is_array() && !to_json_array(args).empty() &&
        !obj_expr.is_nil() && is_typescript_string_type(obj_expr.type()))
      {
        exprt str_arg = convert_expression(*to_json_array(args).begin());
        if(is_typescript_string_type(str_arg.type()))
        {
          // For constant pattern and constant string: check at
          // conversion time.
          std::string pattern_sv = extract_string_value(obj_expr);
          std::string str_sv = extract_string_value(str_arg);
          if(!pattern_sv.empty() && !str_sv.empty())
          {
            std::string pattern = pattern_sv.substr(2);
            std::string str = str_sv.substr(2);
            // Phase 2: try NFA-based metacharacter match.
            std::optional<bool> nfa_result =
              typescript_regex::match(pattern, str);
            if(nfa_result.has_value())
              return *nfa_result ? exprt{true_exprt{}} : exprt{false_exprt{}};
            // Phase 1 fallback: literal-substring semantics.
            // (Only reached when pattern uses an unsupported feature
            // such as |, (, ), or { — see typescript_regex.h.)
            return str.find(pattern) != std::string::npos
                     ? exprt{true_exprt{}}
                     : exprt{false_exprt{}};
          }
          // Solver path: route through cprover_string_contains_func.
          // Note: contains(haystack, needle) — the string arg is the
          // haystack, the pattern (obj_expr) is the needle.
          exprt refined_str = ts_string_to_refined(str_arg);
          exprt refined_pattern = ts_string_to_refined(obj_expr);
          refined_string_typet refined_ty =
            to_refined_string_type(refined_str.type());
          irep_idt func_id = ID_cprover_string_contains_func;
          if(symbol_table.lookup(func_id) == nullptr)
          {
            std::vector<typet> arg_types = {refined_ty, refined_ty};
            mathematical_function_typet ft(std::move(arg_types), bool_typet{});
            symbolt fs{func_id, ft, "typescript"};
            fs.base_name = id2string(func_id);
            symbol_table.add(fs);
          }
          function_application_exprt app(
            symbol_exprt{func_id, symbol_table.lookup_ref(func_id).type},
            {refined_str, refined_pattern});
          app.type() = bool_typet{};
          return std::move(app);
        }
      }
      // When either the receiver or the needle is symbolic we route
      // through the refined string solver via
      // cprover_string_contains_func. Receiver and needle both get
      // converted via ts_string_to_refined (which emits the
      // solver-side associations as pending statements).
      if(
        (method == "includes" || method == "startsWith" ||
         method == "endsWith") &&
        args.is_array() && !to_json_array(args).empty() && !obj_expr.is_nil() &&
        is_typescript_string_type(obj_expr.type()))
      {
        exprt needle_arg = convert_expression(*to_json_array(args).begin());
        if(is_typescript_string_type(needle_arg.type()))
        {
          exprt refined_self = ts_string_to_refined(obj_expr);
          exprt refined_needle = ts_string_to_refined(needle_arg);
          refined_string_typet refined_ty =
            to_refined_string_type(refined_self.type());
          irep_idt func_id =
            method == "includes"     ? ID_cprover_string_contains_func
            : method == "startsWith" ? ID_cprover_string_is_prefix_func
                                     : ID_cprover_string_is_suffix_func;
          // Declare the function.
          if(symbol_table.lookup(func_id) == nullptr)
          {
            std::vector<typet> arg_types = {refined_ty, refined_ty};
            mathematical_function_typet ft(std::move(arg_types), bool_typet{});
            symbolt fs{func_id, ft, "typescript"};
            fs.base_name = id2string(func_id);
            symbol_table.add(fs);
          }
          function_application_exprt::argumentst call_args;
          if(method == "includes")
          {
            // cprover_string_contains_func(str, substr): true iff
            // substr occurs in str. Arg order matches our semantics:
            // self.includes(needle).
            call_args = {refined_self, refined_needle};
          }
          else
          {
            // cprover_string_is_prefix_func(prefix, str) and
            // cprover_string_is_suffix_func(suffix, str) — solver
            // convention puts the sub-string FIRST. Our semantics is
            // self.startsWith(needle) = "needle is a prefix of self",
            // so pass needle as prefix (arg 0) and self as str
            // (arg 1).
            call_args = {refined_needle, refined_self};
          }
          function_application_exprt app(
            symbol_exprt{func_id, symbol_table.lookup_ref(func_id).type},
            std::move(call_args));
          app.type() = bool_typet{};
          return std::move(app);
        }
      }
      // ES2024 §22.1.3.17/18: padStart/padEnd on a symbolic receiver
      // (e.g. function parameter typed as string). Emit length =
      // max(src.length, n); content is nondet.
      if(
        !obj_expr.is_nil() && is_typescript_string_type(obj_expr.type()) &&
        (method == "padStart" || method == "padEnd") && args.is_array() &&
        !to_json_array(args).empty())
      {
        exprt target_arg = convert_expression(*to_json_array(args).begin());
        struct_typet str_type = typescript_string_type();
        typet len_type = str_type.components()[0].type();
        // Cast both to the length type via IEEE float.
        exprt src_len_i = member_exprt{obj_expr, "length", len_type};
        // target_arg is an IEEE float. Compute max in float then cast.
        exprt src_len_f = typecast_exprt{src_len_i, double_type()};
        exprt cond = binary_relation_exprt{target_arg, ID_gt, src_len_f};
        exprt result_len_f = if_exprt{cond, target_arg, src_len_f};
        exprt result_len = typecast_exprt{result_len_f, len_type};
        typet data_type = to_array_type(str_type.components()[1].type());
        array_typet data_arr_type = to_array_type(data_type);
        exprt::operandst nondet_chars;
        while(nondet_chars.size() < TYPESCRIPT_MAX_STRING_LENGTH)
          nondet_chars.push_back(
            side_effect_expr_nondett{unsignedbv_typet{16}, source_locationt{}});
        return struct_exprt{
          {result_len, array_exprt{std::move(nondet_chars), data_arr_type}},
          str_type};
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
        exprt end_arg = nil_exprt{};
        if(it != to_json_array(args).end())
          end_arg = convert_expression(*it);
        struct_typet str_type = typescript_string_type();
        const auto &data_type = to_array_type(str_type.components()[1].type());
        exprt data = member_exprt{obj_expr, "data", data_type};
        exprt len_e = member_exprt{obj_expr, "length", signedbv_typet{32}};
        if(start.type() != signedbv_typet{32})
          start = typecast_exprt{start, signedbv_typet{32}};
        exprt end_e;
        // Both nil_exprt and a default-constructed exprt signal
        // "no end provided". Default-constructed exprts have an
        // empty id, NOT ID_nil, so check both.
        if(end_arg.is_nil() || end_arg.id().empty())
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
      // ES2024 §22.1.3.3: charCodeAt on a non-constant receiver.
      // Access obj_expr.data[idx] and cast to float. This is the
      // path that fires when the receiver is symbolic (e.g. after
      // __CPROVER_assume(s === "literal") — the assume constrains
      // the data at the SAT level, so reading data[idx] gives the
      // correct character code).
      if(
        method == "charCodeAt" && args.is_array() &&
        !to_json_array(args).empty())
      {
        exprt idx_expr = convert_expression(*to_json_array(args).begin());
        if(idx_expr.type().id() == ID_floatbv)
          idx_expr = typecast_exprt{idx_expr, signedbv_typet{64}};
        else if(idx_expr.type() != signedbv_typet{64})
          idx_expr = typecast_exprt{idx_expr, signedbv_typet{64}};
        struct_typet str_type = typescript_string_type();
        const auto &data_type = to_array_type(str_type.components()[1].type());
        exprt data = member_exprt{obj_expr, "data", data_type};
        exprt char_val = index_exprt{data, idx_expr};
        // Cast unsignedbv[16] → floatbv[64]
        return typecast_exprt{char_val, double_type()};
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
      // ES2024 §22.1.3.22: String.prototype.slice on a non-constant
      // receiver or with non-constant indices. Route through the
      // refined-string solver's cprover_string_substring_func.
      if(method == "slice" && args.is_array() && !to_json_array(args).empty())
      {
        const auto &arg_arr = to_json_array(args);
        auto ait = arg_arr.begin();
        exprt start_expr = convert_expression(*ait);
        ++ait;
        // End defaults to length.
        exprt end_expr =
          ait != arg_arr.end()
            ? convert_expression(*ait)
            : exprt{member_exprt{obj_expr, "length", signedbv_typet{32}}};
        // Convert to the solver's index type (signedbv[64]).
        if(start_expr.type().id() == ID_floatbv)
          start_expr = typecast_exprt{start_expr, signedbv_typet{64}};
        if(end_expr.type().id() == ID_floatbv)
          end_expr = typecast_exprt{end_expr, signedbv_typet{64}};
        else if(end_expr.type() != signedbv_typet{64})
          end_expr = typecast_exprt{end_expr, signedbv_typet{64}};
        exprt refined = ts_string_to_refined(obj_expr);
        return ts_call_string_returning_function(
          ID_cprover_string_substring_func, {refined, start_expr, end_expr});
      }
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
        // split("non-empty") on non-constant string: return a nondet
        // array of strings of bounded length. We can't model the actual
        // separator semantics symbolically, but returning a struct of
        // the right shape is sound (over-approximating: each element
        // is an arbitrary string) and makes for-of iteration work.
        struct_typet str_type = typescript_string_type();
        array_typet arr_type{
          str_type,
          from_integer(TYPESCRIPT_MAX_ARRAY_LENGTH, signedbv_typet{64})};
        struct_typet list_type = make_array_struct_type(arr_type);
        return side_effect_expr_nondett{list_type, get_location(node)};
      }
      // Nondet fallback for non-constant strings
      return side_effect_expr_nondett{double_type(), get_location(node)};
    }
    // ES2024 sec-array.prototype.map
    // ES2024 §23.1.3.12 Array.prototype.forEach.
    // Calls callback with (element, index, array) for each element
    // in order. Returns undefined. The callback can mutate outer
    // state (sum += v) — so we must emit actual CALLs at runtime,
    // not conversion-time unrolling of pure expressions. We lift
    // the calls as pending_stmts (drained by the enclosing
    // statement) and return a nil expression (the result is
    // discarded).
    if(
      !obj_expr.is_nil() && obj_expr.type().id() == ID_struct &&
      to_struct_type(obj_expr.type()).get_tag() == "typescript_array" &&
      method == "forEach" && args.is_array() && !to_json_array(args).empty())
    {
      const jsont &callback = *to_json_array(args).begin();
      static unsigned fe_ctr = 0;
      std::string cb_name = "__ts_fe_cb_" + std::to_string(fe_ctr++);
      convert_function_declaration_with_name(callback, cb_name);
      irep_idt cb_id{"typescript::" + cb_name};
      const symbolt *cb_sym = symbol_table.lookup(cb_id);
      // Resolve src array.
      exprt src = obj_expr;
      if(src.id() == ID_symbol)
      {
        const symbolt *s =
          symbol_table.lookup(to_symbol_expr(src).get_identifier());
        if(s && !s->value.is_nil())
          src = s->value;
      }
      if(
        cb_sym != nullptr && src.id() == ID_struct &&
        src.operands().size() >= 2)
      {
        mp_integer len{0};
        if(src.operands()[0].is_constant())
          to_integer(to_constant_expr(src.operands()[0]), len);
        const exprt &data = src.operands()[1];
        const code_typet &cb_type = to_code_type(cb_sym->type);
        // Emit a CALL per element; the callback's side effects
        // (e.g. mutating a captured `sum`) apply in order.
        for(mp_integer i = 0; i < len; ++i)
        {
          auto ci = i.to_ulong();
          if(ci >= data.operands().size())
            break;
          exprt::operandst call_args;
          call_args.push_back(data.operands()[ci]);
          // Optionally pass the index if the callback declares one.
          if(cb_type.parameters().size() >= 2)
          {
            exprt idx = from_integer(i, double_type());
            call_args.push_back(idx);
          }
          code_function_callt call{cb_sym->symbol_expr(), std::move(call_args)};
          call.add_source_location() = get_location(node);
          pending_stmts.push_back(std::move(call));
        }
      }
      return nil_exprt{};
    }

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

        // Detect whether start/end args are constant.
        int start_idx = 0, end_idx = src_len.to_long();
        bool start_constant = true, end_constant = true;
        exprt start_sym, end_sym;
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
          else if(
            sv.id() == ID_unary_minus && !sv.operands().empty() &&
            sv.operands()[0].is_constant() &&
            sv.operands()[0].type().id() == ID_floatbv)
          {
            ieee_floatt fv{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            fv.from_expr(to_constant_expr(sv.operands()[0]));
            start_idx = -static_cast<int>(std::stod(fv.to_ansi_c_string()));
          }
          else
          {
            start_constant = false;
            start_sym = sv;
            if(start_sym.type().id() == ID_floatbv)
              start_sym = typecast_exprt{start_sym, signedbv_typet{64}};
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
          else if(
            ev.id() == ID_unary_minus && !ev.operands().empty() &&
            ev.operands()[0].is_constant() &&
            ev.operands()[0].type().id() == ID_floatbv)
          {
            ieee_floatt fv{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            fv.from_expr(to_constant_expr(ev.operands()[0]));
            end_idx = -static_cast<int>(std::stod(fv.to_ansi_c_string()));
          }
          else
          {
            end_constant = false;
            end_sym = ev;
            if(end_sym.type().id() == ID_floatbv)
              end_sym = typecast_exprt{end_sym, signedbv_typet{64}};
          }
        }
        int src_long = static_cast<int>(src_len.to_long());
        if(start_constant)
        {
          // ES2024 §23.1.3.27 step 4: negative = from end
          if(start_idx < 0)
            start_idx = std::max(0, src_long + start_idx);
          if(start_idx > src_long)
            start_idx = src_long;
        }
        if(end_constant)
        {
          if(end_idx < 0)
            end_idx = std::max(0, src_long + end_idx);
          if(end_idx > src_long)
            end_idx = src_long;
        }
        if(start_constant && end_constant && start_idx > end_idx)
          end_idx = start_idx;

        typet elem_type = double_type();
        if(!data.operands().empty())
          elem_type = data.operands()[0].type();

        std::size_t max_len = TYPESCRIPT_MAX_ARRAY_LENGTH;
        array_typet arr_type{
          elem_type, from_integer(max_len, signedbv_typet{64})};
        struct_typet list_type = make_array_struct_type(arr_type);

        // Constant case: resolve at conversion time.
        if(start_constant && end_constant)
        {
          exprt::operandst result_elts;
          for(int i = start_idx; i < end_idx; ++i)
          {
            if(static_cast<std::size_t>(i) < data.operands().size())
              result_elts.push_back(data.operands()[i]);
          }
          std::size_t actual = result_elts.size();
          while(result_elts.size() < max_len)
            result_elts.push_back(from_integer(0, elem_type));
          return struct_exprt{
            {from_integer(actual, signedbv_typet{64}),
             array_exprt{std::move(result_elts), arr_type}},
            list_type};
        }

        // Symbolic case: emit a result whose length is a symbolic
        // expression and whose data[i] is (i < result_len) ?
        // src.data[start + i] : 0.
        exprt start_expr =
          start_constant ? exprt{from_integer(start_idx, signedbv_typet{64})}
                         : start_sym;
        exprt end_expr = end_constant
                           ? exprt{from_integer(end_idx, signedbv_typet{64})}
                           : end_sym;
        // Clamp negative: max(0, src_long + x) for negative; clamp
        // to src_long for overflow. For symbolic, we use if_exprts.
        auto clamp = [&](const exprt &x, bool is_constant_arg) -> exprt
        {
          if(is_constant_arg)
            return x;
          // if(x < 0) max(0, src_long + x) else min(x, src_long)
          exprt src_long_e = from_integer(src_long, signedbv_typet{64});
          exprt zero_e = from_integer(0, signedbv_typet{64});
          exprt neg_clamped = if_exprt{
            binary_relation_exprt{plus_exprt{src_long_e, x}, ID_lt, zero_e},
            zero_e,
            plus_exprt{src_long_e, x}};
          exprt pos_clamped = if_exprt{
            binary_relation_exprt{x, ID_gt, src_long_e}, src_long_e, x};
          return if_exprt{
            binary_relation_exprt{x, ID_lt, zero_e}, neg_clamped, pos_clamped};
        };
        start_expr = clamp(start_expr, start_constant);
        end_expr = clamp(end_expr, end_constant);
        // result_len = max(0, end - start)
        exprt zero_e = from_integer(0, signedbv_typet{64});
        exprt diff = minus_exprt{end_expr, start_expr};
        exprt result_len =
          if_exprt{binary_relation_exprt{diff, ID_lt, zero_e}, zero_e, diff};
        // Build data: for each slot i, data[i] = (i < result_len) ?
        // src.data[start + i] : 0. We can only index src.data with
        // constant slots (it's an array_exprt), so we build an
        // if-chain over all source slot choices.
        exprt::operandst filled;
        for(std::size_t i = 0; i < max_len; ++i)
        {
          exprt i_expr = from_integer(i, signedbv_typet{64});
          // For slot i of result: data[start + i] (which could be
          // any src slot). Build an if_exprt chain: if start == 0,
          // src.data[i]; if start == 1, src.data[i+1]; etc.
          exprt src_val = from_integer(0, elem_type);
          for(int src_i = src_long - 1; src_i >= 0; --src_i)
          {
            // src_i = start + i → start = src_i - i
            int want_start = src_i - static_cast<int>(i);
            if(want_start < 0 || want_start >= src_long)
              continue;
            exprt start_matches = equal_exprt{
              start_expr, from_integer(want_start, signedbv_typet{64})};
            if(static_cast<std::size_t>(src_i) < data.operands().size())
              src_val =
                if_exprt{start_matches, data.operands()[src_i], src_val};
          }
          exprt in_range = binary_relation_exprt{i_expr, ID_lt, result_len};
          filled.push_back(
            if_exprt{in_range, src_val, from_integer(0, elem_type)});
        }
        return struct_exprt{
          {result_len, array_exprt{std::move(filled), arr_type}}, list_type};
      }
    }
    // ES2024 sec-array.prototype.find
    // Array.find: return first element matching predicate
    if(
      !obj_expr.is_nil() && obj_expr.type().id() == ID_struct &&
      to_struct_type(obj_expr.type()).get_tag() == "typescript_array" &&
      (method == "find" || method == "findLast") && args.is_array() &&
      !to_json_array(args).empty())
    {
      bool is_last = (method == "findLast");
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
        // For findLast, iterate in reverse so the FIRST match
        // encountered is the LAST in source order.
        std::vector<mp_integer> order;
        if(is_last)
          for(mp_integer i = len - 1; i >= 0; --i)
            order.push_back(i);
        else
          for(mp_integer i = 0; i < len; ++i)
            order.push_back(i);
        for(const mp_integer &i : order)
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
    // Array.findIndex / findLastIndex: find index of first/last matching element
    if(
      !obj_expr.is_nil() && obj_expr.type().id() == ID_struct &&
      to_struct_type(obj_expr.type()).get_tag() == "typescript_array" &&
      (method == "findIndex" || method == "findLastIndex") && args.is_array() &&
      !to_json_array(args).empty())
    {
      bool is_last_idx = (method == "findLastIndex");
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
        // For findLastIndex, iterate in reverse.
        std::vector<mp_integer> order_fi;
        if(is_last_idx)
          for(mp_integer i = len - 1; i >= 0; --i)
            order_fi.push_back(i);
        else
          for(mp_integer i = 0; i < len; ++i)
            order_fi.push_back(i);
        for(const mp_integer &i : order_fi)
        {
          auto idx = i.to_ulong();
          if(idx >= data.operands().size())
            continue;
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
    // ES2024 §23.1.3.12: Array.prototype.includes(searchElement,
    // fromIndex). Negative fromIndex adds length; result clamped to
    // [0, length]. Uses SameValueZero (treats +0 === -0 and NaN ===
    // NaN); we approximate with strict === since our number model
    // doesn't distinguish these.
    if(
      !obj_expr.is_nil() && obj_expr.type().id() == ID_struct &&
      to_struct_type(obj_expr.type()).get_tag() == "typescript_array" &&
      method == "includes" && args.is_array() && !to_json_array(args).empty())
    {
      const auto &arg_arr = to_json_array(args);
      auto it = arg_arr.begin();
      exprt target = convert_expression(*it++);
      int from_idx_val = 0;
      bool from_idx_specified = false;
      if(it != arg_arr.end())
      {
        exprt v = convert_expression(*it);
        from_idx_specified = true;
        if(v.is_constant() && v.type().id() == ID_floatbv)
        {
          ieee_floatt fv{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          fv.from_expr(to_constant_expr(v));
          from_idx_val = static_cast<int>(std::stod(fv.to_ansi_c_string()));
        }
        else if(
          v.id() == ID_unary_minus && !v.operands().empty() &&
          v.operands()[0].is_constant() &&
          v.operands()[0].type().id() == ID_floatbv)
        {
          ieee_floatt fv{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          fv.from_expr(to_constant_expr(v.operands()[0]));
          from_idx_val = -static_cast<int>(std::stod(fv.to_ansi_c_string()));
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
        int start = 0;
        if(from_idx_specified)
        {
          start = from_idx_val;
          if(start < 0)
            start += len.to_long();
          if(start < 0)
            start = 0;
        }
        // ES2024 §23.1.3.12 uses SameValueZero: NaN === NaN is true.
        // Detect the "target is NaN" case and use isNaN(element)
        // instead of strict IEEE compare (which would give false).
        bool target_is_nan = false;
        if(target.is_constant() && target.type().id() == ID_floatbv)
        {
          ieee_floatt tv{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          tv.from_expr(to_constant_expr(target));
          target_is_nan = tv.is_NaN();
        }
        // OR together equality checks for each element from start.
        exprt result = false_exprt{};
        for(mp_integer i = start; i < len; ++i)
        {
          auto idx = i.to_ulong();
          if(idx >= data.operands().size())
            break;
          exprt eq;
          if(target_is_nan && data.operands()[idx].type().id() == ID_floatbv)
          {
            // Element matches iff it is NaN (x != x is the IEEE NaN
            // test).
            eq = ieee_float_notequal_exprt{
              data.operands()[idx], data.operands()[idx]};
          }
          else
          {
            eq = target.type().id() == ID_floatbv
                   ? exprt{ieee_float_equal_exprt{data.operands()[idx], target}}
                   : exprt{equal_exprt{data.operands()[idx], target}};
          }
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
        // ES2024 §23.1.3.15: empty array joins to the empty string
        // (not to the separator). Always return the constant literal
        // when we got through the constant-folding path, even if the
        // accumulated string is empty.
        if(len == 0 || !result.empty())
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
        // Symbolic-element sort: emit a bubble-sort compare-and-swap
        // network at conversion time. For each pair (j, j+1) in a
        // decreasing series, build:
        //   new[j]   = cond(old[j], old[j+1]) ? old[j+1] : old[j]
        //   new[j+1] = cond(old[j], old[j+1]) ? old[j]   : old[j+1]
        // where cond is `old[j] > old[j+1]` for ascending, `<` for
        // descending. ES2024 §23.1.3.29 + §23.1.3.29.4 SortCompare.
        // Only proceed when the comparator was recognised.
        if(recognized)
        {
          int n = static_cast<int>(len.to_long());
          if(n < 2)
            return obj_expr; // nothing to sort
          // Initial element exprs.
          std::vector<exprt> cur;
          // Detect the element type from the first element (fall
          // back to double_type if indeterminate).
          elem_type = double_type();
          for(mp_integer i = 0; i < len; ++i)
          {
            auto idx = i.to_ulong();
            if(idx < data.operands().size())
            {
              cur.push_back(data.operands()[idx]);
              if(data.operands()[idx].type().id() == ID_floatbv)
                elem_type = data.operands()[idx].type();
            }
            else
              cur.push_back(from_integer(0, elem_type));
          }
          auto compare_greater = [&](const exprt &a, const exprt &b) -> exprt
          {
            if(a.type().id() == ID_floatbv)
              return binary_relation_exprt{a, ID_gt, b};
            return binary_relation_exprt{a, ID_gt, b};
          };
          // Bubble sort network: n-1 passes, each walking from 0 to
          // n-1-pass comparing adjacent pairs.
          for(int pass = 0; pass < n - 1; ++pass)
          {
            for(int j = 0; j < n - 1 - pass; ++j)
            {
              exprt lhs = cur[j];
              exprt rhs = cur[j + 1];
              exprt cond = descending
                             ? compare_greater(rhs, lhs)  // rhs > lhs → swap
                             : compare_greater(lhs, rhs); // lhs > rhs → swap
              cur[j] = if_exprt{cond, rhs, lhs};
              cur[j + 1] = if_exprt{cond, lhs, rhs};
            }
          }
          // Build the sorted struct.
          exprt::operandst sorted_ops = std::move(cur);
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
          if(obj_expr.id() == ID_symbol)
          {
            pending_stmts.push_back(
              code_frontend_assignt{obj_expr, sorted_struct});
          }
          return sorted_struct;
        }
        // Fallthrough: return unsorted original (unrecognised comparator).
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
      // If the inner element type is itself a typescript_array, we
      // flatten one level: iterate over outer elements, and within
      // each inner array copy its elements into the result. Returning
      // a value of a different type than the receiver is essential
      // to avoid solver-level type mismatches downstream.
      if(src.id() == ID_struct && src.operands().size() >= 2)
      {
        const auto &outer_st = to_struct_type(src.type());
        const auto &outer_data_type =
          to_array_type(outer_st.components()[1].type());
        const typet &inner_elem = outer_data_type.element_type();
        bool is_nested =
          inner_elem.id() == ID_struct &&
          to_struct_type(inner_elem).get_tag() == "typescript_array";
        if(is_nested)
        {
          // Collect inner elements at conversion time if both the
          // outer array and each inner array have constant length and
          // inline data.
          mp_integer outer_len{0};
          if(src.operands()[0].is_constant())
            to_integer(to_constant_expr(src.operands()[0]), outer_len);
          const exprt &outer_data = src.operands()[1];
          const auto &inner_st = to_struct_type(inner_elem);
          const auto &inner_data_type =
            to_array_type(inner_st.components()[1].type());
          typet leaf_type = inner_data_type.element_type();
          exprt::operandst result_elts;
          for(mp_integer i = 0; i < outer_len; ++i)
          {
            auto oi = i.to_ulong();
            if(oi >= outer_data.operands().size())
              break;
            const exprt &sub = outer_data.operands()[oi];
            if(sub.id() != ID_struct || sub.operands().size() < 2)
              continue;
            mp_integer sub_len{0};
            if(sub.operands()[0].is_constant())
              to_integer(to_constant_expr(sub.operands()[0]), sub_len);
            const exprt &sub_data = sub.operands()[1];
            for(mp_integer j = 0; j < sub_len; ++j)
            {
              auto sj = j.to_ulong();
              if(sj < sub_data.operands().size())
                result_elts.push_back(sub_data.operands()[sj]);
            }
          }
          std::size_t actual = result_elts.size();
          std::size_t max_len = TYPESCRIPT_MAX_ARRAY_LENGTH;
          while(result_elts.size() < max_len)
            result_elts.push_back(from_integer(0, leaf_type));
          array_typet arr_type{
            leaf_type, from_integer(max_len, signedbv_typet{64})};
          struct_typet list_type = make_array_struct_type(arr_type);
          return struct_exprt{
            {from_integer(actual, signedbv_typet{64}),
             array_exprt{std::move(result_elts), arr_type}},
            list_type};
        }
      }
      // 1-D array (or unknown shape): flat is a no-op.
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
      // ES2024 §23.1.3.7: fill(value, start=0, end=length). Mutates.
      const auto &arg_arr = to_json_array(args);
      auto it = arg_arr.begin();
      exprt fill_val = convert_expression(*it++);
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
        int src_long = static_cast<int>(len.to_long());
        const exprt &data = src.operands()[1];

        auto read_int_arg = [this](const jsont &a, int def) -> int
        {
          exprt v = convert_expression(a);
          if(v.is_constant() && v.type().id() == ID_floatbv)
          {
            ieee_floatt fv{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            fv.from_expr(to_constant_expr(v));
            return static_cast<int>(std::stod(fv.to_ansi_c_string()));
          }
          if(
            v.id() == ID_unary_minus && !v.operands().empty() &&
            v.operands()[0].is_constant() &&
            v.operands()[0].type().id() == ID_floatbv)
          {
            ieee_floatt fv{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            fv.from_expr(to_constant_expr(v.operands()[0]));
            return -static_cast<int>(std::stod(fv.to_ansi_c_string()));
          }
          return def;
        };

        // Check whether start/end args are constant; if not, we'll
        // emit symbolic predicates per slot.
        auto arg_is_constant = [this](const jsont &a) -> bool
        {
          exprt v = convert_expression(a);
          if(v.is_constant() && v.type().id() == ID_floatbv)
            return true;
          if(
            v.id() == ID_unary_minus && !v.operands().empty() &&
            v.operands()[0].is_constant() &&
            v.operands()[0].type().id() == ID_floatbv)
            return true;
          return false;
        };

        bool start_constant = true, end_constant = true;
        exprt start_sym, end_sym;
        int start = 0, end = src_long;
        auto save_it = it;
        if(it != arg_arr.end())
        {
          if(arg_is_constant(*it))
            start = read_int_arg(*it, 0);
          else
          {
            start_constant = false;
            start_sym = convert_expression(*it);
            // Convert to signed int for comparison.
            if(start_sym.type().id() == ID_floatbv)
              start_sym = typecast_exprt{start_sym, signedbv_typet{64}};
          }
          ++it;
        }
        if(it != arg_arr.end())
        {
          if(arg_is_constant(*it))
            end = read_int_arg(*it, src_long);
          else
          {
            end_constant = false;
            end_sym = convert_expression(*it);
            if(end_sym.type().id() == ID_floatbv)
              end_sym = typecast_exprt{end_sym, signedbv_typet{64}};
          }
        }
        (void)save_it;
        // Negative indices count from end (constant case only).
        if(start_constant)
        {
          if(start < 0)
            start = std::max(0, src_long + start);
          if(start > src_long)
            start = src_long;
        }
        if(end_constant)
        {
          if(end < 0)
            end = std::max(0, src_long + end);
          if(end > src_long)
            end = src_long;
        }

        typet elem_type = fill_val.type();
        if(!data.operands().empty())
          elem_type = data.operands()[0].type();
        if(fill_val.type() != elem_type)
          fill_val = typecast_exprt{fill_val, elem_type};

        exprt::operandst filled;
        for(int i = 0; i < src_long; ++i)
        {
          exprt orig = (static_cast<std::size_t>(i) < data.operands().size())
                         ? data.operands()[i]
                         : from_integer(0, elem_type);
          // Build the "in range" predicate — fully constant if both
          // bounds are, otherwise a symbolic conjunction.
          if(start_constant && end_constant)
          {
            if(i >= start && i < end)
              filled.push_back(fill_val);
            else
              filled.push_back(orig);
          }
          else
          {
            exprt i_expr = from_integer(i, signedbv_typet{64});
            exprt cond_start =
              start_constant
                ? (i >= start ? exprt{true_exprt{}} : exprt{false_exprt{}})
                : exprt{binary_relation_exprt{i_expr, ID_ge, start_sym}};
            exprt cond_end =
              end_constant
                ? (i < end ? exprt{true_exprt{}} : exprt{false_exprt{}})
                : exprt{binary_relation_exprt{i_expr, ID_lt, end_sym}};
            exprt in_range = and_exprt{cond_start, cond_end};
            filled.push_back(if_exprt{in_range, fill_val, orig});
          }
        }
        std::size_t max_len = TYPESCRIPT_MAX_ARRAY_LENGTH;
        while(filled.size() < max_len)
          filled.push_back(from_integer(0, elem_type));
        array_typet arr_type{
          elem_type, from_integer(max_len, signedbv_typet{64})};
        struct_typet list_type = make_array_struct_type(arr_type);
        struct_exprt new_arr{
          {from_integer(src_long, signedbv_typet{64}),
           array_exprt{std::move(filled), arr_type}},
          list_type};
        // ES2024: fill mutates in place. Assign back to receiver symbol.
        if(obj_expr.id() == ID_symbol)
        {
          pending_stmts.push_back(code_frontend_assignt{obj_expr, new_arr});
          const symbolt *obj_sym =
            symbol_table.lookup(to_symbol_expr(obj_expr).get_identifier());
          if(obj_sym != nullptr)
            symbol_table.get_writeable(obj_sym->name)->value = new_arr;
        }
        return new_arr;
      }
    }
    // ES2024 §23.1.3.2: Array.prototype.concat(...args).
    // For each arg, if it is an Array (Symbol.isConcatSpreadable
    // defaults to true for arrays), spread its elements one level.
    // Otherwise push the arg as-is. Multiple args accepted.
    if(
      !obj_expr.is_nil() && obj_expr.type().id() == ID_struct &&
      to_struct_type(obj_expr.type()).get_tag() == "typescript_array" &&
      method == "concat" && args.is_array())
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
        mp_integer len1{0};
        if(src.operands()[0].is_constant())
          to_integer(to_constant_expr(src.operands()[0]), len1);
        const exprt &d1 = src.operands()[1];
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
        // Iterate every argument and spread arrays one level.
        bool concat_ok = true;
        for(const auto &arg_node : to_json_array(args))
        {
          exprt arg_expr = convert_expression(arg_node);
          if(arg_expr.id() == ID_symbol)
          {
            const symbolt *as =
              symbol_table.lookup(to_symbol_expr(arg_expr).get_identifier());
            if(as && !as->value.is_nil())
              arg_expr = as->value;
          }
          if(
            arg_expr.type().id() == ID_struct &&
            to_struct_type(arg_expr.type()).get_tag() == "typescript_array" &&
            arg_expr.id() == ID_struct && arg_expr.operands().size() >= 2)
          {
            mp_integer len2{0};
            if(arg_expr.operands()[0].is_constant())
              to_integer(to_constant_expr(arg_expr.operands()[0]), len2);
            const exprt &d2 = arg_expr.operands()[1];
            for(mp_integer i = 0; i < len2; ++i)
            {
              auto idx = i.to_ulong();
              if(idx < d2.operands().size())
                combined.push_back(d2.operands()[idx]);
            }
          }
          else if(
            arg_expr.type().id() == ID_struct &&
            to_struct_type(arg_expr.type()).get_tag() == "typescript_array")
          {
            // Non-literal array (symbolic). We can't spread at
            // conversion time; bail and leave to the fallback.
            concat_ok = false;
            break;
          }
          else if(!arg_expr.is_nil())
          {
            // Scalar or other element type — coerce to the array's
            // element type where possible, then push.
            if(arg_expr.type() != elem_type)
              arg_expr = typecast_exprt{arg_expr, elem_type};
            combined.push_back(arg_expr);
          }
        }
        if(concat_ok)
        {
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
    }
    // ES2024 sec-array.prototype.indexof
    // Array.indexOf: find index of element
    if(
      !obj_expr.is_nil() && obj_expr.type().id() == ID_struct &&
      to_struct_type(obj_expr.type()).get_tag() == "typescript_array" &&
      (method == "indexOf" || method == "lastIndexOf") && args.is_array() &&
      !to_json_array(args).empty())
    {
      const auto &arg_arr = to_json_array(args);
      auto it = arg_arr.begin();
      exprt target = convert_expression(*it++);
      // Optional fromIndex.
      int from_idx_val = 0;
      bool from_idx_specified = false;
      if(it != arg_arr.end())
      {
        exprt v = convert_expression(*it);
        from_idx_specified = true;
        if(v.is_constant() && v.type().id() == ID_floatbv)
        {
          ieee_floatt fv{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          fv.from_expr(to_constant_expr(v));
          from_idx_val = static_cast<int>(std::stod(fv.to_ansi_c_string()));
        }
        else if(
          v.id() == ID_unary_minus && !v.operands().empty() &&
          v.operands()[0].is_constant() &&
          v.operands()[0].type().id() == ID_floatbv)
        {
          ieee_floatt fv{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          fv.from_expr(to_constant_expr(v.operands()[0]));
          from_idx_val = -static_cast<int>(std::stod(fv.to_ansi_c_string()));
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
        int src_long = static_cast<int>(len.to_long());
        const exprt &data = src.operands()[1];
        // Clamp fromIndex; negative counts from end.
        int start = from_idx_specified
                      ? from_idx_val
                      : (method == "lastIndexOf" ? src_long - 1 : 0);
        if(start < 0)
          start = std::max(0, src_long + start);
        if(start > src_long)
          start = src_long;
        if(method == "lastIndexOf" && start > src_long - 1)
          start = src_long - 1;

        auto make_result = [](int result)
        {
          double dv = static_cast<double>(result);
          uint64_t bits;
          std::memcpy(&bits, &dv, sizeof(bits));
          return constant_exprt{
            integer2bvrep(mp_integer{bits}, 64), double_type()};
        };

        // For constant target, we can resolve at conversion time.
        if(target.is_constant())
        {
          if(method == "lastIndexOf")
          {
            for(int i = start; i >= 0; --i)
            {
              if(
                static_cast<std::size_t>(i) < data.operands().size() &&
                data.operands()[i] == target)
                return make_result(i);
            }
          }
          else
          {
            for(int i = start; i < src_long; ++i)
            {
              if(
                static_cast<std::size_t>(i) < data.operands().size() &&
                data.operands()[i] == target)
                return make_result(i);
            }
          }
          return make_result(-1);
        }

        // Symbolic target: build nested if_exprt chain that returns
        // the first matching index (or -1). This matches how Map.has
        // / Set.has handle symbolic keys/values.
        auto elem_equal = [](const exprt &a, const exprt &b) -> exprt
        {
          if(a.type().id() == ID_floatbv && b.type().id() == ID_floatbv)
            return ieee_float_equal_exprt{a, b};
          if(a.type() != b.type())
            return false_exprt{};
          return equal_exprt{a, b};
        };
        exprt result = make_result(-1);
        if(method == "lastIndexOf")
        {
          // Iterate forward so later matches overwrite earlier.
          for(int i = 0; i <= start; ++i)
          {
            if(static_cast<std::size_t>(i) >= data.operands().size())
              break;
            exprt cond = elem_equal(data.operands()[i], target);
            result = if_exprt{cond, make_result(i), result};
          }
        }
        else
        {
          // Iterate from high index to low, so the LAST if_exprt
          // wraps all smaller indices as the "else" — picks the
          // smallest matching index.
          for(int i = src_long - 1; i >= start; --i)
          {
            if(static_cast<std::size_t>(i) >= data.operands().size())
              continue;
            exprt cond = elem_equal(data.operands()[i], target);
            result = if_exprt{cond, make_result(i), result};
          }
        }
        return result;
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
          // ES2024 §23.1.3.30: splice(start, deleteCount, ...items)
          // mutates: removes deleteCount elements at `start`, inserts
          // `items` in their place. For constant arrays, we compute
          // the full result at conversion time.
          const auto &arg_arr = to_json_array(args);
          if(arg_arr.empty())
            return obj_expr;
          // Resolve source to its constant value if possible.
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
            typet elem_type = double_type();
            if(!data.operands().empty())
              elem_type = data.operands()[0].type();

            auto read_int_arg = [this](const jsont &a, int def) -> int
            {
              exprt v = convert_expression(a);
              if(v.is_constant() && v.type().id() == ID_floatbv)
              {
                ieee_floatt fv{
                  ieee_float_spect::double_precision(),
                  ieee_floatt::rounding_modet::ROUND_TO_EVEN};
                fv.from_expr(to_constant_expr(v));
                return static_cast<int>(std::stod(fv.to_ansi_c_string()));
              }
              if(
                v.id() == ID_unary_minus && !v.operands().empty() &&
                v.operands()[0].is_constant() &&
                v.operands()[0].type().id() == ID_floatbv)
              {
                ieee_floatt fv{
                  ieee_float_spect::double_precision(),
                  ieee_floatt::rounding_modet::ROUND_TO_EVEN};
                fv.from_expr(to_constant_expr(v.operands()[0]));
                return -static_cast<int>(std::stod(fv.to_ansi_c_string()));
              }
              return def;
            };

            auto it = arg_arr.begin();
            int src_long = static_cast<int>(src_len.to_long());
            // Check whether start and deleteCount are constants.
            // If deleteCount is symbolic, skip the constant path so
            // the symbolic-fallback handler below can build a
            // per-slot if_exprt chain.
            bool args_all_const = true;
            {
              auto ck_it = arg_arr.begin();
              exprt se = convert_expression(*ck_it);
              if(
                !(se.is_constant() && se.type().id() == ID_floatbv) &&
                !(se.id() == ID_unary_minus && !se.operands().empty() &&
                  se.operands()[0].is_constant() &&
                  se.operands()[0].type().id() == ID_floatbv))
                args_all_const = false;
              ++ck_it;
              if(ck_it != arg_arr.end())
              {
                exprt de = convert_expression(*ck_it);
                if(
                  !(de.is_constant() && de.type().id() == ID_floatbv) &&
                  !(de.id() == ID_unary_minus && !de.operands().empty() &&
                    de.operands()[0].is_constant() &&
                    de.operands()[0].type().id() == ID_floatbv))
                  args_all_const = false;
              }
            }
            if(args_all_const)
            {
              (void)src_long;
              int start = read_int_arg(*it++, 0);
              // Negative start counts from end.
              if(start < 0)
                start = std::max(0, src_long + start);
              if(start > src_long)
                start = src_long;
              int del_count = src_long - start;
              if(it != arg_arr.end())
              {
                del_count = read_int_arg(*it++, 0);
                if(del_count < 0)
                  del_count = 0;
                if(del_count > src_long - start)
                  del_count = src_long - start;
              }
              // Collect inserted items.
              exprt::operandst inserted;
              while(it != arg_arr.end())
              {
                exprt v = convert_expression(*it++);
                if(v.type() != elem_type)
                  v = typecast_exprt{v, elem_type};
                inserted.push_back(v);
              }
              // Build result array: before + inserted + after.
              exprt::operandst result_elts;
              for(int i = 0; i < start; i++)
                if(static_cast<std::size_t>(i) < data.operands().size())
                  result_elts.push_back(data.operands()[i]);
              for(auto &v : inserted)
                result_elts.push_back(v);
              for(int i = start + del_count; i < src_long; i++)
                if(static_cast<std::size_t>(i) < data.operands().size())
                  result_elts.push_back(data.operands()[i]);
              int actual = static_cast<int>(result_elts.size());
              std::size_t max_len = TYPESCRIPT_MAX_ARRAY_LENGTH;
              while(result_elts.size() < max_len)
                result_elts.push_back(from_integer(0, elem_type));
              array_typet arr_type{
                elem_type, from_integer(max_len, signedbv_typet{64})};
              struct_typet list_type = make_array_struct_type(arr_type);
              struct_exprt new_arr{
                {from_integer(actual, signedbv_typet{64}),
                 array_exprt{std::move(result_elts), arr_type}},
                list_type};
              // Mutate in place: assign back to receiver.
              if(obj_expr.id() == ID_symbol)
              {
                pending_stmts.push_back(
                  code_frontend_assignt{obj_expr, new_arr});
                const symbolt *obj_sym = symbol_table.lookup(
                  to_symbol_expr(obj_expr).get_identifier());
                if(obj_sym != nullptr)
                  symbol_table.get_writeable(obj_sym->name)->value = new_arr;
              }
              return new_arr;
            } // end if(args_all_const)
          }
          // Fallback: for a constant source array with a constant start
          // and symbolic deleteCount, shift elements via a per-slot
          // if_exprt chain. result[i] = (i + start + del_count < src_len)
          //                             ? src[i + start + del_count]
          //                             : 0
          // length = src_len - del_count.
          if(arg_arr.size() >= 2)
          {
            // Resolve source to constant
            exprt src2 = obj_expr;
            if(src2.id() == ID_symbol)
            {
              const symbolt *s =
                symbol_table.lookup(to_symbol_expr(src2).get_identifier());
              if(s && !s->value.is_nil())
                src2 = s->value;
            }
            auto it = arg_arr.begin();
            exprt start_expr = convert_expression(*it++);
            exprt del_count_expr = convert_expression(*it);
            // Try to detect constant start. (Symbolic start is harder.)
            mp_integer start_mp{0};
            bool start_const = false;
            if(start_expr.is_constant() && start_expr.type().id() == ID_floatbv)
            {
              ieee_floatt fv{
                ieee_float_spect::double_precision(),
                ieee_floatt::rounding_modet::ROUND_TO_EVEN};
              fv.from_expr(to_constant_expr(start_expr));
              start_mp = mp_integer{
                static_cast<long long>(std::stod(fv.to_ansi_c_string()))};
              start_const = true;
            }
            if(
              start_const && src2.id() == ID_struct &&
              src2.operands().size() >= 2 && src2.operands()[0].is_constant() &&
              src2.operands()[1].id() == ID_array)
            {
              mp_integer src_len{0};
              to_integer(to_constant_expr(src2.operands()[0]), src_len);
              const exprt &data = src2.operands()[1];
              typet elem_type = data.operands().empty()
                                  ? double_type()
                                  : data.operands()[0].type();
              typet idx_type = signedbv_typet{64};
              // Per ES2024 ToIntegerOrInfinity: truncate towards zero
              // via typecast, matching spec behaviour for non-integer
              // arguments.
              exprt dc_int = typecast_exprt{del_count_expr, idx_type};
              int start_i = static_cast<int>(start_mp.to_long());
              int src_len_i = static_cast<int>(src_len.to_long());
              exprt::operandst new_elts;
              for(int i = 0; i < static_cast<int>(data.operands().size()); ++i)
              {
                if(i < start_i)
                {
                  new_elts.push_back(data.operands()[i]);
                  continue;
                }
                // offset = start_i + (i - start_i) + dc = i + dc.
                exprt offset = plus_exprt{from_integer(i, idx_type), dc_int};
                // Guard: offset < src_len_i.
                exprt guard = binary_relation_exprt{
                  offset, ID_lt, from_integer(src_len_i, idx_type)};
                // src[offset]. Use symbolic indexing on the original
                // data array (inside src2).
                exprt elt = index_exprt{data, offset};
                new_elts.push_back(
                  if_exprt{guard, elt, from_integer(0, elem_type)});
              }
              array_typet arr_type = to_array_type(data.type());
              exprt new_len =
                minus_exprt{from_integer(src_len_i, idx_type), dc_int};
              struct_typet list_type = to_struct_type(obj_expr.type());
              struct_exprt new_arr{
                {new_len, array_exprt{std::move(new_elts), arr_type}},
                list_type};
              if(obj_expr.id() == ID_symbol)
              {
                pending_stmts.push_back(
                  code_frontend_assignt{obj_expr, new_arr});
              }
              return new_arr;
            }
            // Pure fallback: length-only update for arrays where we
            // can't reason about contents.
            exprt len = member_exprt{obj_expr, "length", signedbv_typet{64}};
            if(start_expr.type() != signedbv_typet{64})
              start_expr = typecast_exprt{start_expr, signedbv_typet{64}};
            if(del_count_expr.type() != signedbv_typet{64})
              del_count_expr =
                typecast_exprt{del_count_expr, signedbv_typet{64}};
            pending_stmts.push_back(
              code_frontend_assignt{len, minus_exprt{len, del_count_expr}});
          }
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
          // ES2024 §24.1.3.3: delete the entry with matching key.
          // Swap-and-pop: find the key, overwrite with last element,
          // decrement size. has() then correctly returns false.
          exprt key = convert_expression(*to_json_array(args).begin());
          const auto &keys_arr_type_d =
            to_array_type(mst.get_component("keys").type());
          if(key.type() != keys_arr_type_d.element_type())
            key = typecast_exprt{key, keys_arr_type_d.element_type()};
          static unsigned del_ctr = 0;
          unsigned dc = del_ctr++;
          std::string flag_n = "__ts_mdel_found_" + std::to_string(dc);
          irep_idt flag_id{"typescript::" + flag_n};
          if(symbol_table.lookup(flag_id) == nullptr)
          {
            symbolt fs{flag_id, bool_typet{}, "typescript"};
            fs.base_name = flag_n;
            fs.is_lvalue = true;
            fs.is_state_var = true;
            fs.is_static_lifetime = true;
            symbol_table.add(fs);
          }
          symbol_exprt flag{flag_id, bool_typet{}};
          pending_stmts.push_back(code_frontend_assignt{flag, false_exprt{}});
          for(int i = 0; i < 8; i++)
          {
            exprt idx = from_integer(i, signedbv_typet{64});
            exprt in_range = binary_relation_exprt{idx, ID_lt, size_m};
            exprt key_i = index_exprt{keys_m, idx};
            exprt match = equal_exprt{key_i, key};
            exprt cond = and_exprt{in_range, and_exprt{match, not_exprt{flag}}};
            // last_idx = size - 1 (captured from current size).
            exprt last_idx =
              minus_exprt{size_m, from_integer(1, signedbv_typet{64})};
            pending_stmts.push_back(code_ifthenelset{
              cond,
              code_blockt{
                {code_frontend_assignt{
                   index_exprt{keys_m, idx}, index_exprt{keys_m, last_idx}},
                 code_frontend_assignt{
                   index_exprt{vals_m, idx}, index_exprt{vals_m, last_idx}},
                 code_frontend_assignt{flag, true_exprt{}}}}});
          }
          pending_stmts.push_back(code_ifthenelset{
            flag,
            code_blockt{{code_frontend_assignt{
              size_m,
              minus_exprt{size_m, from_integer(1, signedbv_typet{64})}}}}});
          return flag;
        }
        if(method == "clear")
        {
          // ES2024 §24.1.3.1: just zero the size; has/get linear scans
          // check i < size, so no further cleanup needed for correctness.
          pending_stmts.push_back(
            code_frontend_assignt{size_m, from_integer(0, signedbv_typet{64})});
          return obj_expr;
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
          const auto &data_arr_type =
            to_array_type(mst.get_component("data").type());
          if(val.type() != data_arr_type.element_type())
            val = typecast_exprt{val, data_arr_type.element_type()};
          // exists = ∃ i. i < size && data[i] == val
          exprt exists = false_exprt{};
          for(int i = 7; i >= 0; i--)
          {
            exprt idx = from_integer(i, signedbv_typet{64});
            exprt in_range = binary_relation_exprt{idx, ID_lt, size_s};
            exprt match = equal_exprt{index_exprt{data_s, idx}, val};
            exists = or_exprt{exists, and_exprt{in_range, match}};
          }
          // If not exists: data[size] = val; size = size + 1
          pending_stmts.push_back(code_ifthenelset{
            not_exprt{exists},
            code_blockt{
              {code_frontend_assignt{index_exprt{data_s, size_s}, val},
               code_frontend_assignt{
                 size_s,
                 plus_exprt{size_s, from_integer(1, signedbv_typet{64})}}}}});
          return obj_expr;
        }
        if(method == "has" && args.is_array() && !to_json_array(args).empty())
        {
          // ES2024 sec-set.prototype.has: linear scan of data[0..size)
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
        if(
          method == "delete" && args.is_array() && !to_json_array(args).empty())
        {
          // ES2024 §24.2.3.4: swap-and-pop delete.
          exprt val = convert_expression(*to_json_array(args).begin());
          const auto &data_arr_type =
            to_array_type(mst.get_component("data").type());
          if(val.type() != data_arr_type.element_type())
            val = typecast_exprt{val, data_arr_type.element_type()};
          static unsigned sdel_ctr = 0;
          unsigned dc = sdel_ctr++;
          std::string flag_n = "__ts_sdel_found_" + std::to_string(dc);
          irep_idt flag_id{"typescript::" + flag_n};
          if(symbol_table.lookup(flag_id) == nullptr)
          {
            symbolt fs{flag_id, bool_typet{}, "typescript"};
            fs.base_name = flag_n;
            fs.is_lvalue = true;
            fs.is_state_var = true;
            fs.is_static_lifetime = true;
            symbol_table.add(fs);
          }
          symbol_exprt flag{flag_id, bool_typet{}};
          pending_stmts.push_back(code_frontend_assignt{flag, false_exprt{}});
          for(int i = 0; i < 8; i++)
          {
            exprt idx = from_integer(i, signedbv_typet{64});
            exprt in_range = binary_relation_exprt{idx, ID_lt, size_s};
            exprt match = equal_exprt{index_exprt{data_s, idx}, val};
            exprt cond = and_exprt{in_range, and_exprt{match, not_exprt{flag}}};
            exprt last_idx =
              minus_exprt{size_s, from_integer(1, signedbv_typet{64})};
            pending_stmts.push_back(code_ifthenelset{
              cond,
              code_blockt{
                {code_frontend_assignt{
                   index_exprt{data_s, idx}, index_exprt{data_s, last_idx}},
                 code_frontend_assignt{flag, true_exprt{}}}}});
          }
          pending_stmts.push_back(code_ifthenelset{
            flag,
            code_blockt{{code_frontend_assignt{
              size_s,
              minus_exprt{size_s, from_integer(1, signedbv_typet{64})}}}}});
          return flag;
        }
        if(method == "clear")
        {
          pending_stmts.push_back(
            code_frontend_assignt{size_s, from_integer(0, signedbv_typet{64})});
          return obj_expr;
        }
      }
    }
    // ES2024 §20.1.3.2: Object.prototype.hasOwnProperty("key")
    // Works on any struct (plain object, interface, etc.).
    if(
      !obj_expr.is_nil() && obj_expr.type().id() == ID_struct &&
      method == "hasOwnProperty" && args.is_array() &&
      !to_json_array(args).empty())
    {
      // Skip internal struct types — only plain objects / interfaces.
      const auto &st = to_struct_type(obj_expr.type());
      std::string tag = id2string(st.get_tag());
      if(
        tag != "typescript_array" && tag != "typescript_string" &&
        tag != "typescript_union" && tag != "typescript_tuple" &&
        tag.substr(0, 17) != "typescript_class_")
      {
        exprt key_arg = convert_expression(*to_json_array(args).begin());
        std::string key = extract_string_value(key_arg);
        if(!key.empty())
        {
          std::string key_name = key.substr(2);
          for(const auto &c : st.components())
          {
            if(id2string(c.get_name()) == key_name)
              return true_exprt{};
          }
          return false_exprt{};
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
    // ES2024 §26.1.3.2: WeakRef.prototype.deref() — always returns
    // the referent (no GC in bounded model checking).
    if(method == "deref" && !obj_expr.is_nil())
      return obj_expr;

    // ES2024 §27.5.3.2: Generator.prototype.next().
    // When the receiver has a generator struct type (tagged
    // "typescript_generator"), next() reads __values[__state],
    // increments __state, and returns { value, done }.
    if(
      !obj_expr.is_nil() && obj_expr.type().id() == ID_struct &&
      to_struct_type(obj_expr.type()).get_tag() == "typescript_generator" &&
      method == "next")
    {
      const auto &gst = to_struct_type(obj_expr.type());
      exprt state = member_exprt{obj_expr, "__state", signedbv_typet{32}};
      exprt count = member_exprt{obj_expr, "__count", signedbv_typet{32}};
      exprt values = member_exprt{
        obj_expr, "__values", gst.get_component("__values").type()};
      // value = __values[__state] (read BEFORE increment)
      // Since pending_stmts (which contain the increment) are
      // drained before the enclosing statement, we increment first
      // and read at __state - 1.
      typet elem_type =
        to_array_type(gst.get_component("__values").type()).element_type();
      // Increment state first (via pending_stmts).
      if(obj_expr.id() == ID_symbol)
      {
        pending_stmts.push_back(code_frontend_assignt{
          member_exprt{obj_expr, "__state", signedbv_typet{32}},
          plus_exprt{state, from_integer(1, signedbv_typet{32})}});
      }
      // Read at __state - 1 (the pre-increment value).
      exprt read_idx = minus_exprt{state, from_integer(1, signedbv_typet{32})};
      exprt value =
        index_exprt{values, typecast_exprt{read_idx, signedbv_typet{64}}};
      // done = (__state - 1) >= __count (i.e. we've exhausted all yields)
      exprt done = binary_relation_exprt{read_idx, ID_ge, count};
      // Return { value, done } struct.
      struct_typet result_type;
      result_type.components().push_back(
        struct_typet::componentt{"value", elem_type});
      result_type.components().push_back(
        struct_typet::componentt{"done", bool_typet{}});
      result_type.set_tag("typescript_iterator_result");
      return struct_exprt{{value, done}, result_type};
    }

    // ES2024 §21.4.3.1: Date.now() — returns current time in ms.
    // Modelled as nondet >= 0.
    if(obj_name == "Date" && method == "now")
    {
      static unsigned datenow_ctr = 0;
      std::string name = "__ts_date_now_s_" + std::to_string(datenow_ctr++);
      std::string qname =
        "typescript::" +
        (current_function.empty() ? "" : current_function + "::") + name;
      irep_idt id{qname};
      if(symbol_table.lookup(id) == nullptr)
      {
        symbolt s{id, double_type(), "typescript"};
        s.base_name = name;
        s.is_lvalue = true;
        s.is_state_var = true;
        symbol_table.add(s);
      }
      symbol_exprt sym = symbol_table.lookup_ref(id).symbol_expr();
      pending_stmts.push_back(code_frontend_assignt{
        sym, side_effect_expr_nondett{double_type(), source_locationt{}}});
      pending_stmts.push_back(code_assumet{binary_relation_exprt{
        sym,
        ID_ge,
        ieee_floatt::zero(ieee_float_spect::double_precision()).to_expr()}});
      return sym;
    }

    // ES2024 §21.4.4: Date instance methods.
    // The receiver is a Date struct; extract the time field.
    if(!obj_expr.is_nil() && is_typescript_date_type(obj_expr.type()))
    {
      exprt time_field = member_exprt{obj_expr, "time", double_type()};
      // getTime() / valueOf() — return the raw timestamp.
      if(method == "getTime" || method == "valueOf")
        return time_field;
      // getFullYear(), getMonth(), getDate(), getDay(),
      // getHours(), getMinutes(), getSeconds(), getMilliseconds()
      // — these require epoch-to-calendar conversion which is
      // complex. Return nondet bounded to valid ranges.
      // getFullYear: approximate via epoch arithmetic.
      // ES2024 §21.4.1.3: year from time value. Simplified:
      // days = floor(time / 86400000); year ≈ 1970 + days/365.25
      // This is an approximation; leap year handling would need the
      // full MakeDay algorithm. For verification purposes, the
      // approximation is usually sufficient.
      if(method == "getFullYear")
      {
        // time / 86400000 → days since epoch
        exprt ms_per_day = from_integer(86400000, double_type());
        exprt rm = symbol_exprt{"__CPROVER_rounding_mode", signedbv_typet{32}};
        ieee_float_op_exprt days{time_field, ID_floatbv_div, ms_per_day, rm};
        days.type() = double_type();
        // days / 365.25 → approximate years
        ieee_floatt days_per_year{
          ieee_float_spect::double_precision(),
          ieee_floatt::rounding_modet::ROUND_TO_EVEN};
        days_per_year.from_double(365.25);
        ieee_float_op_exprt years{
          days, ID_floatbv_div, days_per_year.to_expr(), rm};
        years.type() = double_type();
        // floor(years) + 1970
        ieee_float_op_exprt result{
          years, ID_floatbv_plus, from_integer(1970, double_type()), rm};
        result.type() = double_type();
        return std::move(result);
      }
      // ES2024 §21.4.1.4–8: time-of-day getters via integer modular
      // arithmetic. Cast the (double) timestamp to int64 first; this
      // is exact for any timestamp representable as a 64-bit integer
      // (covers the entire JS-relevant range ±10^15 ms).
      //
      // For getDate/getMonth we use the Howard Hinnant
      // (chrono/civil) algorithm: a closed-form conversion from
      // days-since-epoch to (year, month, day) that avoids
      // year-by-year iteration. See
      // http://howardhinnant.github.io/date_algorithms.html
      auto cast_d2i = [&](const exprt &e) -> exprt {
        return typecast_exprt{e, signedbv_typet{64}};
      };
      auto i2d = [&](const exprt &e) -> exprt {
        return typecast_exprt{e, double_type()};
      };
      auto i64 = [&](long long v) -> exprt
      { return from_integer(v, signedbv_typet{64}); };

      if(method == "getMilliseconds")
      {
        // time mod 1000
        exprt t = cast_d2i(time_field);
        return i2d(mod_exprt{t, i64(1000)});
      }
      if(method == "getSeconds")
      {
        // floor(time / 1000) mod 60
        exprt t = cast_d2i(time_field);
        exprt sec = div_exprt{t, i64(1000)};
        return i2d(mod_exprt{sec, i64(60)});
      }
      if(method == "getMinutes")
      {
        // floor(time / 60000) mod 60
        exprt t = cast_d2i(time_field);
        exprt minute = div_exprt{t, i64(60000)};
        return i2d(mod_exprt{minute, i64(60)});
      }
      if(method == "getHours")
      {
        // floor(time / 3600000) mod 24
        exprt t = cast_d2i(time_field);
        exprt hour = div_exprt{t, i64(3600000)};
        return i2d(mod_exprt{hour, i64(24)});
      }
      if(method == "getDay")
      {
        // (floor(time / 86400000) + 4) mod 7
        // Jan 1 1970 was Thursday (= 4).
        exprt t = cast_d2i(time_field);
        exprt days = div_exprt{t, i64(86400000)};
        exprt shifted = plus_exprt{days, i64(4), signedbv_typet{64}};
        return i2d(mod_exprt{shifted, i64(7)});
      }
      if(method == "getMonth" || method == "getDate")
      {
        // Howard Hinnant civil_from_days. Computes (year, month, day)
        // from days-since-epoch via closed-form integer arithmetic.
        //
        //   days = floor(time / 86400000)
        //   z    = days + 719468
        //   era  = z / 146097                       (assumes z >= 0)
        //   doe  = z - era * 146097                 [0, 146096]
        //   yoe  = (doe - doe/1460 + doe/36524 - doe/146096) / 365
        //                                            [0, 399]
        //   doy  = doe - (365*yoe + yoe/4 - yoe/100) [0, 365]
        //   mp   = (5*doy + 2) / 153                 [0, 11]
        //   d    = doy - (153*mp + 2)/5 + 1          [1, 31]
        //   m    = mp < 10 ? mp + 3 : mp - 9         [1, 12]
        //
        // Caveat: this assumes time >= 0. For negative timestamps,
        // the algorithm needs a corrected `era` formula. We treat
        // negative-time correctness as a known imprecision (rare in
        // practice).
        signedbv_typet i64t{64};
        exprt t = cast_d2i(time_field);
        exprt days = div_exprt{t, i64(86400000)};
        exprt z = plus_exprt{days, i64(719468), i64t};
        exprt era = div_exprt{z, i64(146097)};
        // doe = z - era * 146097
        exprt doe = minus_exprt{z, mult_exprt{era, i64(146097)}};
        // yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365
        exprt doe_div_1460 = div_exprt{doe, i64(1460)};
        exprt doe_div_36524 = div_exprt{doe, i64(36524)};
        exprt doe_div_146096 = div_exprt{doe, i64(146096)};
        exprt yoe_num1 = minus_exprt{doe, doe_div_1460};
        exprt yoe_num2 = plus_exprt{yoe_num1, doe_div_36524, i64t};
        exprt yoe_num3 = minus_exprt{yoe_num2, doe_div_146096};
        exprt yoe = div_exprt{yoe_num3, i64(365)};
        // doy = doe - (365*yoe + yoe/4 - yoe/100)
        exprt yoe_x_365 = mult_exprt{yoe, i64(365)};
        exprt yoe_div_4 = div_exprt{yoe, i64(4)};
        exprt yoe_div_100 = div_exprt{yoe, i64(100)};
        exprt doy_sub1 = plus_exprt{yoe_x_365, yoe_div_4, i64t};
        exprt doy_sub2 = minus_exprt{doy_sub1, yoe_div_100};
        exprt doy = minus_exprt{doe, doy_sub2};
        // mp = (5*doy + 2) / 153
        exprt doy_x_5 = mult_exprt{doy, i64(5)};
        exprt mp_num = plus_exprt{doy_x_5, i64(2), i64t};
        exprt mp = div_exprt{mp_num, i64(153)};

        if(method == "getDate")
        {
          // d = doy - (153*mp + 2)/5 + 1
          exprt mp_x_153 = mult_exprt{mp, i64(153)};
          exprt d_sub_num = plus_exprt{mp_x_153, i64(2), i64t};
          exprt d_sub = div_exprt{d_sub_num, i64(5)};
          exprt d_minus = minus_exprt{doy, d_sub};
          exprt d = plus_exprt{d_minus, i64(1), i64t};
          return i2d(d);
        }
        // method == "getMonth"
        // m = mp < 10 ? mp + 3 : mp - 9, then 0-index for JS
        // i.e. js_month = (mp < 10) ? mp + 2 : mp - 10
        exprt cond = binary_relation_exprt{mp, ID_lt, i64(10)};
        exprt then_branch = plus_exprt{mp, i64(2), i64t};
        exprt else_branch = minus_exprt{mp, i64(10)};
        exprt js_month = if_exprt{cond, then_branch, else_branch};
        return i2d(js_month);
      }
      // toISOString(), toString(), toLocaleDateString() — nondet string.
      if(
        method == "toISOString" || method == "toString" ||
        method == "toLocaleDateString" || method == "toLocaleString" ||
        method == "toLocaleTimeString" || method == "toDateString" ||
        method == "toTimeString" || method == "toUTCString")
        return side_effect_expr_nondett{
          typescript_string_type(), get_location(node)};
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
      if(all_const)
      {
        double res = 0;
        bool ok = true;
        // Zero-argument Math methods: hypot() → 0.
        if(arg_vals.empty())
        {
          if(method == "hypot")
            res = 0.0;
          else
            ok = false;
        }
        else if(method == "sqrt")
        {
          // ES2024 §21.3.2.32: sqrt of negative returns NaN.
          if(arg_vals[0] < 0)
          {
            return ts_nan_with_payload(TS_NAN_PAYLOAD_REAL);
          }
          res = std::sqrt(arg_vals[0]);
        }
        else if(method == "abs")
          res = std::fabs(arg_vals[0]);
        else if(method == "floor")
          res = std::floor(arg_vals[0]);
        else if(method == "ceil")
          res = std::ceil(arg_vals[0]);
        else if(method == "round")
        {
          // ES2024 §21.3.2.29: round rounds ties toward +infinity.
          // std::round rounds ties away from zero (so -0.5 → -1).
          // We need -0.5 → 0. Implementation: floor(x + 0.5).
          res = std::floor(arg_vals[0] + 0.5);
        }
        else if(method == "trunc")
          res = std::trunc(arg_vals[0]);
        else if(method == "sign")
        {
          if(arg_vals[0] > 0.0)
            res = 1.0;
          else if(arg_vals[0] < 0.0)
            res = -1.0;
          else
            res = arg_vals[0]; // +0 or -0 preserved
        }
        else if(method == "cbrt")
          res = std::cbrt(arg_vals[0]);
        else if(method == "hypot" && arg_vals.size() >= 1)
        {
          double sq = 0.0;
          for(double v : arg_vals)
            sq += v * v;
          res = std::sqrt(sq);
        }
        else if(method == "tan")
          res = std::tan(arg_vals[0]);
        else if(method == "asin")
          res = std::asin(arg_vals[0]);
        else if(method == "acos")
          res = std::acos(arg_vals[0]);
        else if(method == "atan")
          res = std::atan(arg_vals[0]);
        else if(method == "atan2" && arg_vals.size() >= 2)
          res = std::atan2(arg_vals[0], arg_vals[1]);
        else if(method == "log2")
          res = std::log2(arg_vals[0]);
        else if(method == "log10")
          res = std::log10(arg_vals[0]);
        else if(method == "log1p")
          res = std::log1p(arg_vals[0]);
        else if(method == "expm1")
          res = std::expm1(arg_vals[0]);
        else if(method == "imul" && arg_vals.size() >= 2)
        {
          // ES2024 §21.3.2.19: Math.imul(a, b) is (a * b) mod 2^32,
          // as a signed int32. Convert to uint32 first (per ToUint32
          // in §7.1.8). NOTE: we can't use arg_vals directly because
          // to_ansi_c_string loses precision on values > 1e7, which
          // defeats the whole point of imul. Re-evaluate operands
          // against the raw constant expressions instead.
          auto to_uint32 =
            [](const jsont &a, typescript_convertert *self) -> uint32_t
          {
            exprt val = self->convert_expression(a);
            const exprt *ce = &val;
            if(ce->id() == ID_typecast && ce->operands().size() == 1)
              ce = &ce->operands()[0];
            bool neg = false;
            if(ce->id() == ID_unary_minus && ce->operands().size() == 1)
            {
              neg = true;
              ce = &ce->operands()[0];
              if(ce->id() == ID_typecast && ce->operands().size() == 1)
                ce = &ce->operands()[0];
            }
            if(ce->is_constant() && ce->type().id() == ID_floatbv)
            {
              ieee_floatt fv{
                ieee_float_spect::double_precision(),
                ieee_floatt::rounding_modet::ROUND_TO_EVEN};
              fv.from_expr(to_constant_expr(*ce));
              mp_integer mi = fv.to_integer();
              int64_t v = mi.to_long();
              if(neg)
                v = -v;
              return static_cast<uint32_t>(v);
            }
            return 0;
          };
          const auto &arg_arr = to_json_array(args);
          auto it2 = arg_arr.begin();
          uint32_t ua = to_uint32(*it2, this);
          ++it2;
          uint32_t ub = to_uint32(*it2, this);
          int32_t product = static_cast<int32_t>(ua * ub);
          res = static_cast<double>(product);
        }
        else if(method == "clz32")
        {
          // ES2024 §21.3.2.10: Math.clz32(x) is the number of
          // leading zeros in ToUint32(x). 0 → 32. Avoid routing
          // through arg_vals because large values lose precision
          // via to_ansi_c_string.
          uint32_t u = 0;
          const auto &arg_arr = to_json_array(args);
          exprt val = convert_expression(*arg_arr.begin());
          const exprt *ce = &val;
          if(ce->id() == ID_typecast && ce->operands().size() == 1)
            ce = &ce->operands()[0];
          bool neg = false;
          if(ce->id() == ID_unary_minus && ce->operands().size() == 1)
          {
            neg = true;
            ce = &ce->operands()[0];
            if(ce->id() == ID_typecast && ce->operands().size() == 1)
              ce = &ce->operands()[0];
          }
          if(ce->is_constant() && ce->type().id() == ID_floatbv)
          {
            ieee_floatt fv{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            fv.from_expr(to_constant_expr(*ce));
            mp_integer mi = fv.to_integer();
            int64_t v = mi.to_long();
            if(neg)
              v = -v;
            u = static_cast<uint32_t>(v);
          }
          if(u == 0)
            res = 32.0;
          else
          {
            int count = 0;
            while((u & 0x80000000u) == 0 && count < 32)
            {
              u <<= 1;
              count++;
            }
            res = static_cast<double>(count);
          }
        }
        else if(method == "sinh")
          res = std::sinh(arg_vals[0]);
        else if(method == "cosh")
          res = std::cosh(arg_vals[0]);
        else if(method == "tanh")
          res = std::tanh(arg_vals[0]);
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
      // Symbolic Math operations for non-constant args. We only
      // implement methods whose symbolic encoding is simple and
      // correct: abs, sign, max, min. Methods like floor/ceil/trunc
      // require float-to-int typecast semantics that are subtle; we
      // leave those to the nondet fallback for symbolic input (users
      // needing them should constrain the input to a constant).
      if(!to_json_array(args).empty())
      {
        auto it = to_json_array(args).begin();
        exprt arg0 = convert_expression(*it);
        if(!arg0.is_nil())
        {
          if(arg0.type() != double_type())
            arg0 = typecast_exprt{arg0, double_type()};
          auto make_double = [](double v)
          {
            ieee_floatt fv{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            fv.from_double(v);
            return fv.to_expr();
          };
          exprt fzero = make_double(0.0);
          exprt fneg1 = make_double(-1.0);
          exprt fpos1 = make_double(1.0);
          if(method == "abs")
          {
            return if_exprt{
              binary_relation_exprt{arg0, ID_ge, fzero},
              arg0,
              unary_minus_exprt{arg0}};
          }
          if(method == "sign")
          {
            return if_exprt{
              binary_relation_exprt{arg0, ID_gt, fzero},
              fpos1,
              if_exprt{binary_relation_exprt{arg0, ID_lt, fzero}, fneg1, arg0}};
          }
          // ES2024 §21.3.2: floor/ceil/trunc/round via the CBMC primitive
          // floatbv_round_to_integral_exprt. Matches how the C frontend
          // encodes floor(), ceil(), trunc() (in src/ansi-c/library/math.c).
          // Rounding modes: FE_TONEAREST=0, FE_DOWNWARD=1, FE_UPWARD=2,
          // FE_TOWARDZERO=3. ES2024 Math.round uses ties-to-+infinity,
          // which differs from FE_TONEAREST (ties-to-even) — we keep the
          // constant-evaluated special case above for exact semantics,
          // and here use the nearest symbolic approximation.
          if(
            method == "floor" || method == "ceil" || method == "trunc" ||
            method == "round")
          {
            int mode = 0;
            if(method == "floor")
              mode = 1; // FE_DOWNWARD
            else if(method == "ceil")
              mode = 2; // FE_UPWARD
            else if(method == "trunc")
              mode = 3; // FE_TOWARDZERO
            else
              mode = 0; // FE_TONEAREST — closest to ES spec for round
            return floatbv_round_to_integral_exprt{
              arg0, from_integer(mode, signedbv_typet{32})};
          }
          if(method == "max" || method == "min")
          {
            exprt result = arg0;
            ++it;
            while(it != to_json_array(args).end())
            {
              exprt next = convert_expression(*it);
              if(!next.is_nil())
              {
                if(next.type() != double_type())
                  next = typecast_exprt{next, double_type()};
                exprt cond = binary_relation_exprt{
                  result, method == "max" ? ID_ge : ID_le, next};
                result = if_exprt{cond, result, next};
              }
              ++it;
            }
            return result;
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
      // Get type parameter names (in declaration order)
      std::vector<std::string> tp_order;
      const jsont &tp0 = json_member(gen_it->second, "typeParameters");
      if(tp0.is_array())
        for(const auto &t : to_json_array(tp0))
        {
          std::string n = json_string(json_member(t, "_type"));
          if(!n.empty())
            tp_order.push_back(n);
        }
      std::set<std::string> tp_names(tp_order.begin(), tp_order.end());
      // For multi-type-param generics, build a map from each type
      // parameter name to its concrete type at the call site. Sources,
      // in order of preference:
      //   1. Explicit typeArguments at the call site: fn<A, B>(...)
      //   2. Inference from arg types matching each param
      std::map<std::string, std::string> type_map;
      const jsont &type_args = json_member(node, "typeArguments");
      if(type_args.is_array())
      {
        std::size_t i = 0;
        for(const auto &ta : to_json_array(type_args))
        {
          if(i >= tp_order.size())
            break;
          std::string concrete = json_string(json_member(ta, "_type"));
          if(!concrete.empty())
            type_map[tp_order[i]] = concrete;
          ++i;
        }
      }
      // Fill any missing type-params by inference from arguments.
      if(type_map.size() < tp_order.size())
      {
        const jsont &params = json_member(gen_it->second, "parameters");
        if(params.is_array() && args.is_array())
        {
          auto param_it = to_json_array(params).begin();
          auto arg_it = to_json_array(args).begin();
          while(param_it != to_json_array(params).end() &&
                arg_it != to_json_array(args).end())
          {
            std::string pt = json_string(json_member(*param_it, "_type"));
            if(tp_names.count(pt) > 0 && type_map.count(pt) == 0)
            {
              std::string concrete = json_string(json_member(*arg_it, "_type"));
              if(concrete == "true" || concrete == "false")
                concrete = "boolean";
              if(
                !concrete.empty() &&
                (std::isdigit(concrete[0]) || concrete[0] == '-'))
                concrete = "number";
              if(!concrete.empty())
                type_map[pt] = concrete;
            }
            ++param_it;
            ++arg_it;
          }
        }
      }
      std::string tp_name = tp_order.empty() ? "T" : tp_order[0];
      // Determine concrete type:
      //   1. Check if return type mentions the type parameter → use return type
      //   2. Otherwise, infer from first argument that has T as its type
      std::string call_type;
      if(type_map.count(tp_name) > 0)
      {
        call_type = type_map[tp_name];
      }
      else
      {
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
      // Include every type-arg in the specialized name so different
      // concrete-type combinations get distinct instantiations.
      std::string full_name_suffix = safe_type;
      if(type_map.size() > 1)
      {
        full_name_suffix.clear();
        for(const auto &name : tp_order)
        {
          auto it = type_map.find(name);
          if(it != type_map.end())
          {
            std::string s = it->second;
            for(char &c : s)
            {
              if(
                c == ' ' || c == '{' || c == '}' || c == ':' || c == ';' ||
                c == '<' || c == '>' || c == ',' || c == '(' || c == ')' ||
                c == '|' || c == '[' || c == ']')
                c = '_';
            }
            if(!full_name_suffix.empty())
              full_name_suffix += "__";
            full_name_suffix += s;
          }
        }
      }
      // Create specialized instance name
      std::string spec_name = func_name + "__" + full_name_suffix;
      irep_idt spec_id{"typescript::" + spec_name};
      // Instantiate if not already done
      if(symbol_table.lookup(spec_id) == nullptr)
      {
        // Convert the generic function with concrete types
        const jsont &gen_node = gen_it->second;
        jsont saved_node = gen_node;
        generic_functions.erase(gen_it);
        // Store the concrete type mapping for this instantiation.
        // Save/restore BOTH the single-param state (for legacy paths)
        // AND the multi-param map.
        std::string saved_generic_type_param = current_generic_type_param;
        std::string saved_generic_concrete = current_generic_concrete;
        auto saved_type_map = current_generic_type_map;
        current_generic_type_param = tp_name;
        current_generic_concrete = call_type;
        if(!type_map.empty())
          current_generic_type_map = type_map;
        convert_function_declaration_with_name(saved_node, spec_name);
        current_generic_type_param = saved_generic_type_param;
        current_generic_concrete = saved_generic_concrete;
        current_generic_type_map = saved_type_map;
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
    {
      // Return a nondet string BOUNDED to a valid length [0, MAX].
      // Create a fresh symbol, initialize with nondet, assume the
      // length field is in range. This is the sound behaviour for
      // symbolic reasoning about strings.
      static unsigned ns_ctr = 0;
      std::string ns_name = "__ts_nondet_str_" + std::to_string(ns_ctr++);
      irep_idt ns_id{"typescript::" + ns_name};
      if(symbol_table.lookup(ns_id) == nullptr)
      {
        symbolt ss{ns_id, typescript_string_type(), "typescript"};
        ss.base_name = ns_name;
        ss.is_lvalue = true;
        ss.is_state_var = true;
        ss.is_static_lifetime = true;
        symbol_table.add(ss);
      }
      symbol_exprt ns_sym{ns_id, typescript_string_type()};
      // Initialize with nondet.
      pending_stmts.push_back(code_frontend_assignt{
        ns_sym,
        side_effect_expr_nondett{
          typescript_string_type(), get_location(node)}});
      // Assume length is in valid range: 0 <= length <= MAX.
      exprt len_member = member_exprt{ns_sym, "length", signedbv_typet{32}};
      pending_stmts.push_back(code_assumet{and_exprt{
        binary_relation_exprt{
          len_member, ID_ge, from_integer(0, signedbv_typet{32})},
        binary_relation_exprt{
          len_member,
          ID_le,
          from_integer(TYPESCRIPT_MAX_STRING_LENGTH, signedbv_typet{32})}}});
      return ns_sym;
    }
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
    // ES2024 §21.2.1.1: BigInt(value) — convert a number or string
    // to bigint. For constant numeric args, cast at conversion time.
    // ES2024 §20.4.1: Symbol() — returns a unique value.
    // Modelled as a unique integer constant (each call site gets a
    // different value). Symbol values are only equal to themselves.
    if(func_name == "Symbol")
    {
      static unsigned symbol_ctr = 1;
      return from_integer(symbol_ctr++, signedbv_typet{64});
    }
    if(func_name == "BigInt")
    {
      if(args.is_array() && !to_json_array(args).empty())
      {
        exprt arg = convert_expression(*to_json_array(args).begin());
        if(arg.type().id() == ID_floatbv && arg.is_constant())
        {
          ieee_floatt fv{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          fv.from_expr(to_constant_expr(arg));
          mp_integer val = fv.to_integer();
          return from_integer(val, typescript_bigint_type());
        }
        // Non-constant: typecast
        return typecast_exprt{arg, typescript_bigint_type()};
      }
      return from_integer(0, typescript_bigint_type());
    }
    if(
      func_name == "parseInt" || func_name == "parseFloat" ||
      func_name == "Number")
    {
      // ES2024 §21.1.2.5, §21.1.2.6, §21.1.1.
      // For constant string args, parse at conversion time.
      if(args.is_array() && !to_json_array(args).empty())
      {
        const auto &arg_arr = to_json_array(args);
        auto it = arg_arr.begin();
        exprt arg = convert_expression(*it);
        // parseInt may have a radix arg.
        int radix = 0; // 0 = auto-detect (decimal unless "0x" prefix)
        bool radix_specified = false;
        if(func_name == "parseInt" && arg_arr.size() >= 2)
        {
          ++it;
          exprt r = convert_expression(*it);
          if(r.is_constant() && r.type().id() == ID_floatbv)
          {
            ieee_floatt rf{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            rf.from_expr(to_constant_expr(r));
            radix = static_cast<int>(rf.to_integer().to_long());
            radix_specified = true;
          }
        }
        // If the arg is already a number, coerce accordingly.
        if(arg.type().id() == ID_floatbv)
        {
          if(func_name == "parseFloat" || func_name == "Number")
            return arg;
          if(func_name == "parseInt" && arg.is_constant())
          {
            ieee_floatt fv{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            fv.from_expr(to_constant_expr(arg));
            ieee_floatt floored = fv.round_to_integral();
            return floored.to_expr();
          }
        }
        std::string sv = extract_string_value(arg);
        if(!sv.empty())
        {
          std::string raw = sv.substr(2);
          // Trim leading/trailing whitespace per spec StringToNumber.
          std::size_t start = 0;
          while(start < raw.size() &&
                std::isspace(static_cast<unsigned char>(raw[start])))
            start++;
          std::size_t end = raw.size();
          while(end > start &&
                std::isspace(static_cast<unsigned char>(raw[end - 1])))
            end--;
          std::string trimmed = raw.substr(start, end - start);
          double d = 0.0;
          bool ok = false;
          try
          {
            if(func_name == "parseInt")
            {
              // Handle "0x"/"0X" prefix when radix is 16 or 0
              // (auto-detect). Spec: §21.1.2.5 step 11.
              std::string s = trimmed;
              bool neg = false;
              if(!s.empty() && (s[0] == '+' || s[0] == '-'))
              {
                neg = s[0] == '-';
                s = s.substr(1);
              }
              if(
                s.size() >= 2 &&
                (s.substr(0, 2) == "0x" || s.substr(0, 2) == "0X"))
              {
                s = s.substr(2);
                // Spec: when radix is unspecified or is 16, the
                // "0x"/"0X" prefix denotes hex. When radix was
                // explicitly something else (e.g. 10), the prefix
                // is still consumed but we respect the user's
                // requested radix (this is not standard JS but
                // matches how most engines behave).
                if(!radix_specified || radix == 16)
                  radix = 16;
              }
              if(radix == 0)
                radix = 10;
              long long iv = std::stoll(s, nullptr, radix);
              d = static_cast<double>(neg ? -iv : iv);
              ok = true;
            }
            else
            {
              // parseFloat or Number: try stod.
              if(func_name == "Number" && trimmed.empty())
              {
                d = 0.0;
                ok = true;
              }
              else
              {
                d = std::stod(trimmed);
                ok = true;
              }
            }
          }
          catch(...)
          {
          }
          if(ok)
          {
            ieee_floatt result{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            result.from_double(d);
            return result.to_expr();
          }
        }
      }
      // Non-constant string arg: route through the solver's
      // cprover_string_parse_int_func for parseInt/Number.
      if(args.is_array() && !to_json_array(args).empty())
      {
        exprt arg = convert_expression(*to_json_array(args).begin());
        if(is_typescript_string_type(arg.type()))
        {
          exprt refined = ts_string_to_refined(arg);
          refined_string_typet refined_ty =
            to_refined_string_type(refined.type());
          if(symbol_table.lookup(ID_cprover_string_parse_int_func) == nullptr)
          {
            std::vector<typet> arg_types = {refined_ty};
            mathematical_function_typet ft(
              std::move(arg_types), signedbv_typet{32});
            symbolt fs{ID_cprover_string_parse_int_func, ft, "typescript"};
            fs.base_name = id2string(ID_cprover_string_parse_int_func);
            symbol_table.add(fs);
          }
          function_application_exprt app(
            symbol_exprt{
              ID_cprover_string_parse_int_func,
              symbol_table.lookup_ref(ID_cprover_string_parse_int_func).type},
            {refined});
          app.type() = signedbv_typet{32};
          return typecast_exprt{app, double_type()};
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
    if(func_name == "__CPROVER_assert_no_path_traversal")
    {
      // Handled at statement level
      return nil_exprt{};
    }
    if(func_name == "__CPROVER_assert_safe_property_key")
    {
      // Handled at statement level
      return nil_exprt{};
    }
    if(func_name == "__CPROVER_assert_not_null")
    {
      // Handled at statement level
      return nil_exprt{};
    }
    if(func_name == "__CPROVER_assert_constant_string")
    {
      // Handled at statement level
      return nil_exprt{};
    }
    if(func_name == "__CPROVER_assert_no_template_metachars")
    {
      // Handled at statement level
      return nil_exprt{};
    }
    if(func_name == "__CPROVER_assert_input_size_bounded")
    {
      // Handled at statement level
      return nil_exprt{};
    }
    if(func_name == "__CPROVER_assert_in_allowlist")
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
    // WeakMap is semantically identical to Map for verification
    // (no GC in bounded model checking).
    class_types["WeakMap"] = map_type;
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
    // WeakMap constructor: same as Map (just initializes size=0).
    {
      std::string wm_ctor = "WeakMap::__init__";
      irep_idt wm_id{"typescript::" + wm_ctor};
      if(symbol_table.lookup(wm_id) == nullptr)
      {
        code_typet::parameterst wm_params;
        code_typet::parametert wm_this{pointer_typet{map_type, 64}};
        wm_this.set_identifier("typescript::" + wm_ctor + "::this");
        wm_this.set_base_name("this");
        wm_params.push_back(wm_this);
        code_typet wm_ft{wm_params, empty_typet{}};
        symbolt wm_sym{wm_id, wm_ft, "typescript"};
        wm_sym.base_name = wm_ctor;
        irep_idt wm_this_id{"typescript::" + wm_ctor + "::this"};
        symbolt wm_this_sym{
          wm_this_id, pointer_typet{map_type, 64}, "typescript"};
        wm_this_sym.base_name = "this";
        wm_this_sym.is_parameter = true;
        wm_this_sym.is_lvalue = true;
        if(symbol_table.lookup(wm_this_id) == nullptr)
          symbol_table.add(wm_this_sym);
        wm_sym.value = code_frontend_assignt{
          member_exprt{
            dereference_exprt{
              symbol_exprt{wm_this_id, pointer_typet{map_type, 64}}},
            "size",
            signedbv_typet{64}},
          from_integer(0, signedbv_typet{64})};
        symbol_table.add(wm_sym);
      }
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
    // Also register Set<string> with string-typed data.
    {
      array_typet str_data{
        typescript_string_type(), from_integer(max_set, signedbv_typet{64})};
      struct_typet str_set;
      str_set.components().push_back(
        struct_typet::componentt{"size", signedbv_typet{64}});
      str_set.components().push_back(
        struct_typet::componentt{"data", str_data});
      str_set.set_tag("typescript_class_Set");
      class_types["Set<string>"] = str_set;
    }
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
  // Emit a warning if many solver-side string allocations were made,
  // suggesting --object-bits 10 to avoid the default 256-object limit.
  if(solver_string_alloc_count > 150)
  {
    log.warning()
      << "This program uses " << solver_string_alloc_count
      << " solver-side string allocations. Consider using "
      << "--object-bits 10 to avoid 'too many addressed objects' errors."
      << messaget::eom;
  }
  return false; // success
}
