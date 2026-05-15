/// \file
/// TypeScript to GOTO converter — implementation
///
/// Converts a TypeScript JSON AST (from ts_ast_to_json.js using the
/// TypeScript Compiler API) into GOTO program symbols.
///
/// Cross-references:
/// - ES2024 = ECMAScript 2024 (ECMA-262), ~/ecma262/spec.html
/// - TSH = TypeScript Handbook, ~/TypeScript-Website/.../handbook-v2/

#include "typescript_converter.h"

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

#include "typescript_types.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>

// --- JSON helpers ---

const jsont &
typescript_convertert::json_member(const jsont &obj, const std::string &key)
{
  static const jsont empty_json;
  if(!obj.is_object())
    return empty_json;
  const auto &object = to_json_object(obj);
  auto it = object.find(key);
  if(it != object.end())
    return it->second;
  return empty_json;
}

std::string typescript_convertert::json_string(const jsont &val)
{
  if(val.is_null())
    return "";
  if(val.is_string() || val.is_number())
    return val.value;
  return "";
}

bool typescript_convertert::is_kind(const jsont &node, const std::string &kind)
{
  return json_string(json_member(node, "_kind")) == kind;
}

source_locationt typescript_convertert::get_location(const jsont &node) const
{
  source_locationt loc;
  loc.set_file(filename);
  const jsont &pos = json_member(node, "_pos");
  if(pos.is_object())
  {
    const jsont &line = json_member(pos, "line");
    if(line.is_number())
      loc.set_line(line.value);
    const jsont &col = json_member(pos, "col");
    if(col.is_number())
      loc.set_column(col.value);
  }
  return loc;
}

// --- Type conversion ---
// ES2024 sec-ecmascript-language-types: ECMAScript Language Types

typet typescript_convertert::convert_type(const std::string &ts_type) const
{
  // Resolve generic type parameters. First check the multi-param map
  // (used when the call site has multiple type arguments), then the
  // single-param context (for legacy single-type-param generics).
  if(!current_generic_type_map.empty())
  {
    auto it = current_generic_type_map.find(ts_type);
    if(it != current_generic_type_map.end())
      return convert_type(it->second);
  }
  if(
    !current_generic_type_param.empty() &&
    ts_type == current_generic_type_param)
    return convert_type(current_generic_concrete);

  // Check cache first
  auto cache_it = type_cache.find(ts_type);
  if(cache_it != type_cache.end())
    return cache_it->second;
  // ES2024 sec-ecmascript-language-types-number-type
  if(ts_type == "number")
    return double_type();
  // ES2024 §6.1.6.2: BigInt type
  if(ts_type == "bigint")
    return bigint_mathematical ? typet{integer_typet{}}
                               : typescript_bigint_type();
  // ES2024 §21.4: Date type
  if(ts_type == "Date")
    return typescript_date_type();
  // ES2024 §22.2: RegExp — modelled as a string (the pattern).
  if(ts_type == "RegExp")
    return typescript_string_type();
  // ES2024 sec-ecmascript-language-types-boolean-type
  if(ts_type == "boolean")
    return bool_typet{};
  // ES2024 sec-ecmascript-language-types-string-type
  if(ts_type == "string")
    return typescript_string_type();
  // TSH: Template Literal Types — treated as string at runtime
  if(!ts_type.empty() && ts_type[0] == '`')
    return typescript_string_type();
  // TSH: String Literal Types — e.g., "circle" or 'square'
  if(
    ts_type.size() >= 2 && ((ts_type[0] == '"' && ts_type.back() == '"') ||
                            (ts_type[0] == '\'' && ts_type.back() == '\'')))
    return typescript_string_type();
  // TSH: Everyday Types > Arrays
  // Array types like "number[]" and also "[T,U][]" — check this
  // suffix FIRST so a type like "[string, number][]" is parsed as
  // an array of tuples rather than as a tuple whose inner gets
  // corrupted by the outer brackets.
  if(ts_type.size() > 2 && ts_type.substr(ts_type.size() - 2) == "[]")
  {
    std::string elem = ts_type.substr(0, ts_type.size() - 2);
    typet elem_type = convert_type(elem);
    std::size_t max_len = TYPESCRIPT_MAX_ARRAY_LENGTH;
    array_typet arr_type{elem_type, from_integer(max_len, signedbv_typet{64})};
    struct_typet list_type;
    list_type.components().push_back(
      struct_typet::componentt{"length", signedbv_typet{64}});
    list_type.components().push_back(
      struct_typet::componentt{"data", arr_type});
    list_type.set_tag("typescript_array");
    type_cache[ts_type] = list_type;
    return list_type;
  }
  // Tuple types: [T, U, V, ...]
  // Homogeneous ([T, T, T]) is treated as number[] equivalent.
  // Heterogeneous ([T, U]) becomes a typescript_tuple struct.
  if(ts_type.size() >= 2 && ts_type.front() == '[' && ts_type.back() == ']')
  {
    std::string inner = ts_type.substr(1, ts_type.size() - 2);
    // Split by ',' at brace depth 0
    std::vector<std::string> elem_types;
    std::size_t pos = 0;
    while(pos < inner.size())
    {
      int depth = 0;
      std::size_t comma = pos;
      while(comma < inner.size())
      {
        char c = inner[comma];
        if(c == '{' || c == '[' || c == '(' || c == '<')
          depth++;
        else if(c == '}' || c == ']' || c == ')' || c == '>')
          depth--;
        else if(c == ',' && depth == 0)
          break;
        comma++;
      }
      std::string etype = inner.substr(pos, comma - pos);
      while(!etype.empty() && etype[0] == ' ')
        etype.erase(0, 1);
      while(!etype.empty() && etype.back() == ' ')
        etype.pop_back();
      if(!etype.empty())
        elem_types.push_back(etype);
      pos = comma + 1;
    }
    // Check homogeneity
    bool homogeneous = true;
    for(std::size_t i = 1; i < elem_types.size(); i++)
      if(elem_types[i] != elem_types[0])
      {
        homogeneous = false;
        break;
      }
    if(homogeneous && !elem_types.empty())
    {
      // Treat as array of the element type.
      return convert_type(elem_types[0] + "[]");
    }
    // Heterogeneous: build tuple struct.
    struct_typet tuple_st;
    for(std::size_t i = 0; i < elem_types.size(); i++)
      tuple_st.components().push_back(struct_typet::componentt{
        "_" + std::to_string(i), convert_type(elem_types[i])});
    tuple_st.set_tag("typescript_tuple");
    return tuple_st;
  }
  // ES2024 sec-ecmascript-language-types-undefined-type
  if(ts_type == "void" || ts_type == "undefined")
    return empty_typet{};
  // ES2024 sec-ecmascript-language-types-function-type
  // TSH: Functions > Function Types
  // Function types: "(x: number) => number"
  if(ts_type.find("=>") != std::string::npos && ts_type[0] == '(')
  {
    auto arrow = ts_type.rfind("=>");
    std::string ret_str = ts_type.substr(arrow + 2);
    while(!ret_str.empty() && ret_str[0] == ' ')
      ret_str.erase(0, 1);
    typet ret_type = convert_type(ret_str);
    code_typet::parameterst params;
    auto paren_end = ts_type.find(')');
    if(paren_end != std::string::npos && paren_end > 1)
    {
      std::string param_str = ts_type.substr(1, paren_end - 1);
      std::istringstream ss(param_str);
      std::string token;
      while(std::getline(ss, token, ','))
      {
        auto colon = token.find(':');
        if(colon != std::string::npos)
        {
          std::string pt = token.substr(colon + 1);
          while(!pt.empty() && pt[0] == ' ')
            pt.erase(0, 1);
          while(!pt.empty() && pt.back() == ' ')
            pt.pop_back();
          params.push_back(code_typet::parametert{convert_type(pt)});
        }
      }
    }
    return pointer_typet{code_typet{params, ret_type}, 64};
  }
  // TSH: Object Types > Object Literal Types
  // Object literal types: { x: number; y: number; }
  if(ts_type.size() > 2 && ts_type[0] == '{' && ts_type.back() == '}')
  {
    // Will cache result at end
    // Parse the type string to extract property names and types
    struct_typet st;
    std::string inner =
      ts_type.substr(2, ts_type.size() - 4); // remove "{ " and " }"
    // Split by "; " respecting nested braces
    std::size_t pos = 0;
    while(pos < inner.size())
    {
      // Find next ';' at brace depth 0
      int depth = 0;
      std::size_t semi = pos;
      while(semi < inner.size())
      {
        if(inner[semi] == '{')
          depth++;
        else if(inner[semi] == '}')
          depth--;
        else if(inner[semi] == ';' && depth == 0)
          break;
        semi++;
      }
      if(semi >= inner.size())
        semi = inner.size();
      std::string field = inner.substr(pos, semi - pos);
      // Trim
      while(!field.empty() && field[0] == ' ')
        field.erase(0, 1);
      while(!field.empty() && field.back() == ' ')
        field.pop_back();
      // Strip 'readonly ' modifier if present
      if(field.substr(0, 9) == "readonly ")
        field.erase(0, 9);
      // Find first ':' at depth 0 (not inside nested type)
      int cdepth = 0;
      std::size_t colon = std::string::npos;
      for(std::size_t ci = 0; ci < field.size(); ++ci)
      {
        if(field[ci] == '{')
          cdepth++;
        else if(field[ci] == '}')
          cdepth--;
        else if(field[ci] == ':' && cdepth == 0)
        {
          colon = ci;
          break;
        }
      }
      if(colon != std::string::npos)
      {
        std::string fname = field.substr(0, colon);
        std::string ftype = field.substr(colon + 1);
        while(!fname.empty() && fname.back() == ' ')
          fname.pop_back();
        // Optional field marker '?' is part of the type shape but not
        // the identifier — strip it so access by property name matches.
        // ES2024 §13.3.9: optional property access is handled via the
        // optional-chaining operator, not the name.
        if(!fname.empty() && fname.back() == '?')
          fname.pop_back();
        while(!ftype.empty() && ftype[0] == ' ')
          ftype.erase(0, 1);
        st.components().push_back(
          struct_typet::componentt{fname, convert_type(ftype)});
      }
      pos = semi + 1;
      while(pos < inner.size() && inner[pos] == ' ')
        pos++;
    }
    return st;
  }
  // Class types
  auto cls_it = class_types.find(ts_type);
  if(cls_it != class_types.end())
    return cls_it->second;
  // Generic class types: Map<K,V>, Set<T>, etc. — strip type parameters
  {
    auto angle = ts_type.find('<');
    if(angle != std::string::npos)
    {
      std::string base = ts_type.substr(0, angle);
      // Promise<T> → T (async is treated as sync)
      if(base == "Promise")
      {
        std::string inner =
          ts_type.substr(angle + 1, ts_type.size() - angle - 2);
        return convert_type(inner);
      }
      // Try specialized class: Box<number> → Box__number
      {
        std::string inner =
          ts_type.substr(angle + 1, ts_type.size() - angle - 2);
        // Trim
        while(!inner.empty() && inner[0] == ' ')
          inner.erase(0, 1);
        while(!inner.empty() && inner.back() == ' ')
          inner.pop_back();
        // Normalize literal types
        if(inner == "true" || inner == "false")
          inner = "boolean";
        else if(!inner.empty() && (std::isdigit(inner[0]) || inner[0] == '-'))
          inner = "number";
        std::string spec_name = base + "__" + inner;
        auto spec_it = class_types.find(spec_name);
        if(spec_it != class_types.end())
          return spec_it->second;
      }
      auto base_it = class_types.find(base);
      if(base_it != class_types.end())
        return base_it->second;
    }
  }
  // ES2024 sec-ecmascript-language-types (union not in spec, TS extension)
  // TSH: Narrowing > typeof type guards, Discriminated Unions
  // Union types: number | string → tagged union struct
  // Need to check for " | " at brace depth 0 to handle { } | { }
  auto find_pipe_at_depth_zero = [](const std::string &s, std::size_t start)
  {
    int depth = 0;
    for(std::size_t i = start; i < s.size(); ++i)
    {
      if(s[i] == '{' || s[i] == '(' || s[i] == '<' || s[i] == '[')
        depth++;
      else if(s[i] == '}' || s[i] == ')' || s[i] == '>' || s[i] == ']')
        depth--;
      else if(
        depth == 0 && i + 2 < s.size() && s[i] == ' ' && s[i + 1] == '|' &&
        s[i + 2] == ' ')
        return i;
    }
    return std::string::npos;
  };
  if(find_pipe_at_depth_zero(ts_type, 0) != std::string::npos)
  {
    // Parse union members
    std::vector<std::string> members;
    std::size_t start = 0;
    while(start < ts_type.size())
    {
      auto pos = find_pipe_at_depth_zero(ts_type, start);
      std::string m;
      if(pos == std::string::npos)
      {
        m = ts_type.substr(start);
        start = ts_type.size();
      }
      else
      {
        m = ts_type.substr(start, pos - start);
        start = pos + 3;
      }
      while(!m.empty() && m[0] == ' ')
        m.erase(0, 1);
      while(!m.empty() && m.back() == ' ')
        m.pop_back();
      if(!m.empty())
        members.push_back(m);
    }
    // If one member is null/undefined and the other is a simple type,
    // strip null (model as the simple type with 0/empty as null sentinel)
    std::vector<std::string> real_members;
    for(const auto &m : members)
      if(m != "null" && m != "undefined")
        real_members.push_back(m);
    if(real_members.size() == 1)
    {
      type_cache[ts_type] = convert_type(real_members[0]);
      return type_cache[ts_type];
    }
    // Check if all members are object types (discriminated union)
    bool all_objects = !real_members.empty();
    for(const auto &m : real_members)
    {
      if(m.empty() || m[0] != '{')
      {
        all_objects = false;
        break;
      }
    }
    if(all_objects)
    {
      // Discriminated union: merge all fields into single struct
      struct_typet merged;
      merged.set_tag("typescript_discriminated_union");
      std::set<std::string> seen;
      for(const auto &m : real_members)
      {
        typet t = convert_type(m);
        if(t.id() == ID_struct)
        {
          for(const auto &c : to_struct_type(t).components())
          {
            std::string cn = id2string(c.get_name());
            if(seen.insert(cn).second)
              merged.components().push_back(c);
          }
        }
      }
      type_cache[ts_type] = merged;
      return merged;
    }
    // Multi-type union: create tagged union struct
    // { __tag: signedbv[32], __num: floatbv[64], __str: refined_string, __bool: bool }
    struct_typet union_type;
    union_type.set_tag("typescript_union");
    union_type.components().push_back(
      struct_typet::componentt{"__tag", signedbv_typet{32}});
    for(std::size_t i = 0; i < real_members.size(); ++i)
    {
      std::string fname = "__v" + std::to_string(i);
      union_type.components().push_back(
        struct_typet::componentt{fname, convert_type(real_members[i])});
    }
    type_cache[ts_type] = union_type;
    return union_type;
  }
  if(false) // old union handler disabled
  {
    // For T | null or T | undefined, use T's type
    // (null/undefined modeled as sentinel values)
    std::string cleaned = ts_type;
    // Remove " | null" and " | undefined"
    auto remove_part = [&](const std::string &part)
    {
      auto pos = cleaned.find(part);
      while(pos != std::string::npos)
      {
        cleaned.erase(pos, part.size());
        pos = cleaned.find(part);
      }
    };
    remove_part(" | null");
    remove_part(" | undefined");
    remove_part("null | ");
    remove_part("undefined | ");
    if(!cleaned.empty())
      return convert_type(cleaned);
  }
  // ES2024 sec-array-objects
  // TSH: Everyday Types > Arrays
  // Array types like "number[]" are handled at the TOP of this
  // function (before the bracket-tuple check) so "[T,U][]" parses
  // as array-of-tuple. No fallback needed here.
  // Default: treat as number for now
  return double_type();
}

// --- Expression conversion ---

exprt typescript_convertert::convert_expression(const jsont &node)
{
  std::string kind = json_string(json_member(node, "_kind"));
  if(kind == "FirstLiteralToken" || kind == "NumericLiteral")
    return convert_numeric_literal(node);
  // ES2024 §6.1.6.2: BigInt literal (e.g. 123n).
  // The text field carries the value with an 'n' suffix (e.g. "123n").
  // Fallback: the _type field also carries it.
  if(kind == "BigIntLiteral")
  {
    std::string val_str = json_string(json_member(node, "text"));
    if(val_str.empty())
      val_str = json_string(json_member(node, "_type"));
    // Strip trailing 'n'
    if(!val_str.empty() && val_str.back() == 'n')
      val_str.pop_back();
    mp_integer val{val_str.c_str()};
    return from_integer(val, typescript_bigint_type());
  }
  if(kind == "StringLiteral" || kind == "NoSubstitutionTemplateLiteral")
    return convert_string_literal(node);
  // ES2024 §22.2: RegExp literal. Store the pattern as a string
  // for use by .test() dispatch. The text field is "/pattern/flags".
  if(kind == "RegularExpressionLiteral")
  {
    std::string text = json_string(json_member(node, "text"));
    // Extract pattern between first and last '/'
    if(text.size() >= 2 && text[0] == '/')
    {
      auto last_slash = text.rfind('/');
      if(last_slash > 0)
      {
        std::string pattern = text.substr(1, last_slash - 1);
        return convert_string_literal_from_text(pattern);
      }
    }
    return convert_string_literal_from_text("");
  }
  if(kind == "TrueKeyword")
    return true_exprt{};
  if(kind == "FalseKeyword")
    return false_exprt{};
  if(kind == "NullKeyword")
  {
    // ES2024 sec-null-value: use NaN as the sentinel for null.
    // This matches undefined (see convert_identifier) so that
    // `null == undefined` and `null ?? x` work correctly. Our model
    // treats null and undefined as interchangeable sentinels
    // (sound for programs that don't distinguish them, which is
    // the common case in TypeScript where the union type
    // T | null | undefined is common).
    return ts_nan_with_payload(TS_NAN_PAYLOAD_NULL);
  }
  if(kind == "Identifier")
    return convert_identifier(node);
  // ES2024 sec-this-keyword
  if(kind == "ThisKeyword")
  {
    // Look up this parameter in current function
    std::string this_id = "typescript::" + current_function + "::this";
    const symbolt *this_sym = symbol_table.lookup(irep_idt{this_id});
    if(this_sym != nullptr)
      return dereference_exprt{this_sym->symbol_expr()};
    return nil_exprt{};
  }
  if(kind == "BinaryExpression")
    return convert_binary_expression(node);
  if(kind == "PrefixUnaryExpression" || kind == "PostfixUnaryExpression")
    return convert_prefix_unary_expression(node);
  // ES2024 sec-arrow-function-definitions
  // Arrow functions in expression context (not variable initializer)
  if(kind == "ArrowFunction" || kind == "FunctionExpression")
  {
    // Convert as a named function and return pointer to it
    static unsigned anon_fn_ctr = 0;
    std::string fn_name = "__anon_fn_" + std::to_string(anon_fn_ctr++);
    convert_function_declaration_with_name(node, fn_name);
    irep_idt fn_id{"typescript::" + fn_name};
    const symbolt *fn_sym = symbol_table.lookup(fn_id);
    if(fn_sym != nullptr)
      return address_of_exprt{fn_sym->symbol_expr()};
    return nil_exprt{};
  }
  if(kind == "CallExpression")
    return convert_call_expression(node);
  // ES2024 sec-property-accessors
  if(kind == "PropertyAccessExpression")
  {
    exprt obj = convert_expression(json_member(node, "expression"));
    std::string prop =
      json_string(json_member(json_member(node, "name"), "text"));
    // Note: the parser emits 'optional' for o?.p (ES2024 §13.3.9
    // optional-chain), but the full short-circuit semantics require
    // runtime undefined-tracking per struct field, which our current
    // struct model doesn't support. Tracked as KNOWNBUG
    // 'optional-chaining'.
    if(is_typescript_string_type(obj.type()) && prop == "length")
    {
      return member_exprt{obj, "length", signedbv_typet{32}};
    }
    if(obj.type().id() == ID_struct)
    {
      const auto &st = to_struct_type(obj.type());
      // Strip # from property name for private field access
      std::string lookup_prop = prop;
      if(!prop.empty() && prop[0] == '#')
        lookup_prop = prop.substr(1);
      if(st.has_component(lookup_prop))
      {
        // Enforce private field access control
        std::string cls_tag = id2string(st.get_tag());
        std::string cls_name_check = cls_tag;
        if(cls_tag.find("typescript_class_") == 0)
          cls_name_check = cls_tag.substr(17);
        auto pf_it = private_fields.find(cls_name_check);
        if(
          pf_it != private_fields.end() && pf_it->second.count(lookup_prop) > 0)
        {
          // Check if we're inside the class
          bool inside_class =
            current_class == cls_name_check ||
            (!current_function.empty() &&
             current_function.find(cls_name_check + "::") == 0);
          if(!inside_class)
          {
            log.error() << "Access to private field '" << prop << "' of class '"
                        << cls_name_check << "' from outside the class"
                        << messaget::eom;
            // Emit assertion failure for verification
            code_assertt priv_assert{false_exprt{}};
            priv_assert.add_source_location() = get_location(node);
            priv_assert.add_source_location().set_property_class(
              "private-access");
            priv_assert.add_source_location().set_comment(
              "access to private field " + prop);
            pending_stmts.push_back(std::move(priv_assert));
          }
        }
        return member_exprt{
          obj, lookup_prop, st.get_component(lookup_prop).type()};
      }
      // Check for getter method: ClassName::prop
      std::string cls_tag = id2string(st.get_tag());
      if(!cls_tag.empty())
      {
        // Strip "typescript_class_" prefix if present
        std::string cls_name_str = cls_tag;
        if(cls_tag.find("typescript_class_") == 0)
          cls_name_str = cls_tag.substr(17);
        irep_idt getter_id{"typescript::" + cls_name_str + "::" + prop};
        const symbolt *getter = symbol_table.lookup(getter_id);
        if(getter != nullptr && getter->type.id() == ID_code)
        {
          const auto &ft = to_code_type(getter->type);
          return side_effect_expr_function_callt{
            symbol_exprt{getter_id, getter->type},
            {address_of_exprt{obj}},
            ft.return_type(),
            get_location(node)};
        }
      }
    }
    // Enum member access: Direction.Down
    {
      std::string obj_name =
        json_string(json_member(json_member(node, "expression"), "text"));
      if(!obj_name.empty())
      {
        std::string enum_qn = "typescript::" + obj_name + "." + prop;
        const symbolt *esym = symbol_table.lookup(irep_idt{enum_qn});
        if(esym != nullptr)
          return esym->symbol_expr();
      }
      // ES2024 §21.3.1: Math.PI, Math.E, Math.LN2, etc.
      if(obj_name == "Math")
      {
        auto make_double = [](double v)
        {
          ieee_floatt fv{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          fv.from_double(v);
          return fv.to_expr();
        };
        if(prop == "PI")
          return make_double(3.141592653589793);
        if(prop == "E")
          return make_double(2.718281828459045);
        if(prop == "LN2")
          return make_double(0.6931471805599453);
        if(prop == "LN10")
          return make_double(2.302585092994046);
        if(prop == "LOG2E")
          return make_double(1.4426950408889634);
        if(prop == "LOG10E")
          return make_double(0.4342944819032518);
        if(prop == "SQRT2")
          return make_double(1.4142135623730951);
        if(prop == "SQRT1_2")
          return make_double(0.7071067811865476);
      }
      // ES2024 §21.1.2: Number.MAX_SAFE_INTEGER, Number.MIN_SAFE_INTEGER, etc.
      if(obj_name == "Number")
      {
        auto make_double = [](double v)
        {
          ieee_floatt fv{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          fv.from_double(v);
          return fv.to_expr();
        };
        if(prop == "MAX_SAFE_INTEGER")
          return make_double(9007199254740991.0);
        if(prop == "MIN_SAFE_INTEGER")
          return make_double(-9007199254740991.0);
        if(prop == "EPSILON")
          return make_double(2.220446049250313e-16);
        if(prop == "MAX_VALUE")
          return make_double(1.7976931348623157e308);
        if(prop == "MIN_VALUE")
          return make_double(5e-324);
        if(prop == "POSITIVE_INFINITY")
        {
          ieee_floatt inf{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          inf.make_plus_infinity();
          return inf.to_expr();
        }
        if(prop == "NEGATIVE_INFINITY")
        {
          ieee_floatt inf{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          inf.make_minus_infinity();
          return inf.to_expr();
        }
        if(prop == "NaN")
        {
          return ts_nan_with_payload(TS_NAN_PAYLOAD_REAL);
        }
      }
    }
    return nil_exprt{};
  }
  // ES2024 sec-element-access: arr[i]
  // ES2024 sec-property-accessors (bracket notation)
  if(kind == "ElementAccessExpression")
  {
    exprt obj = convert_expression(json_member(node, "expression"));
    exprt idx = convert_expression(json_member(node, "argumentExpression"));
    // Tuple access: p[0] on a struct tagged typescript_tuple.
    if(
      !obj.is_nil() && obj.type().id() == ID_struct &&
      to_struct_type(obj.type()).get_tag() == "typescript_tuple" &&
      idx.is_constant() && idx.type().id() == ID_floatbv)
    {
      ieee_floatt fv{
        ieee_float_spect::double_precision(),
        ieee_floatt::rounding_modet::ROUND_TO_EVEN};
      fv.from_expr(to_constant_expr(idx));
      int i = static_cast<int>(std::stod(fv.to_ansi_c_string()));
      const auto &st = to_struct_type(obj.type());
      if(i >= 0 && static_cast<std::size_t>(i) < st.components().size())
      {
        std::string fname = "_" + std::to_string(i);
        return member_exprt{obj, fname, st.components()[i].type()};
      }
    }
    // Computed property access: obj["key"] on struct types
    if(
      !obj.is_nil() && obj.type().id() == ID_struct &&
      to_struct_type(obj.type()).get_tag() != "typescript_array")
    {
      std::string key = extract_string_value(idx);
      if(!key.empty())
      {
        key = key.substr(2);
        const auto &st = to_struct_type(obj.type());
        if(st.has_component(key))
          return member_exprt{obj, key, st.component_type(key)};
      }
    }
    // Bounds check: assert idx >= 0 && idx < length
    if(
      bounds_check && !obj.is_nil() && obj.type().id() == ID_struct &&
      to_struct_type(obj.type()).get_tag() == "typescript_array" &&
      !idx.is_nil())
    {
      exprt len = member_exprt{obj, "length", signedbv_typet{64}};
      exprt idx_int = idx.type().id() == ID_floatbv
                        ? exprt{typecast_exprt{idx, signedbv_typet{64}}}
                        : idx;
      // assert(idx >= 0 && idx < length)
      code_assertt bounds_assert{and_exprt{
        binary_relation_exprt{
          idx_int, ID_ge, from_integer(0, signedbv_typet{64})},
        binary_relation_exprt{idx_int, ID_lt, len}}};
      bounds_assert.add_source_location() = get_location(node);
      bounds_assert.add_source_location().set_property_class("array-bounds");
      bounds_assert.add_source_location().set_comment(
        "array index out of bounds");
      pending_stmts.push_back(std::move(bounds_assert));
    }
    if(!obj.is_nil() && !idx.is_nil())
    {
      // Array indexing: arr[i] → arr.data[i]
      if(obj.type().id() == ID_struct)
      {
        const auto &st = to_struct_type(obj.type());
        if(st.has_component("data"))
        {
          exprt data =
            member_exprt{obj, "data", st.get_component("data").type()};
          if(idx.type() != signedbv_typet{64})
            idx = typecast_exprt(idx, signedbv_typet{64});
          return index_exprt{data, idx};
        }
      }
    }
    return nil_exprt{};
  }
  if(kind == "ParenthesizedExpression")
    return convert_expression(json_member(node, "expression"));
  // ES2024 sec-conditional-operator
  if(kind == "ConditionalExpression")
  {
    exprt cond = convert_expression(json_member(node, "condition"));
    exprt then_e = convert_expression(json_member(node, "whenTrue"));
    exprt else_e = convert_expression(json_member(node, "whenFalse"));
    if(cond.type().id() != ID_bool)
      cond = ts_to_boolean(cond);
    if(then_e.type() != else_e.type())
      else_e = typecast_exprt{else_e, then_e.type()};
    return if_exprt{cond, then_e, else_e};
  }
  // ES2024 sec-new-operator
  // ES2024 sec-new-operator
  // TSH: Classes > Constructor
  if(kind == "NewExpression")
  {
    std::string cls_name =
      json_string(json_member(json_member(node, "expression"), "text"));
    // ES2024 §21.4.2: new Date(...) — construct a Date object.
    if(cls_name == "Date")
    {
      const jsont &call_args = json_member(node, "arguments");
      exprt time_val;
      if(!call_args.is_array() || to_json_array(call_args).empty())
      {
        // new Date() — current time, modelled as nondet >= 0.
        // Allocate a fresh symbol so the assume constraint binds.
        static unsigned date_ctr = 0;
        std::string name = "__ts_date_now_" + std::to_string(date_ctr++);
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
        time_val = sym;
      }
      else
      {
        // new Date(ms) — single numeric argument is the timestamp.
        exprt arg = convert_expression(*to_json_array(call_args).begin());
        if(arg.type().id() != ID_floatbv)
          arg = typecast_exprt{arg, double_type()};
        time_val = arg;
      }
      return struct_exprt{{time_val}, typescript_date_type()};
    }
    // Generic class monomorphization
    auto gen_it = generic_classes.find(cls_name);
    if(gen_it != generic_classes.end())
    {
      // Get concrete type from typeArguments
      std::string concrete_type = "number";
      const jsont &type_args = json_member(node, "typeArguments");
      if(type_args.is_array() && !to_json_array(type_args).empty())
      {
        const jsont &ta = *to_json_array(type_args).begin();
        concrete_type = json_string(json_member(ta, "_type"));
        if(concrete_type.empty())
          concrete_type = "number";
        // Normalize literal types
        if(concrete_type == "true" || concrete_type == "false")
          concrete_type = "boolean";
        else if(
          !concrete_type.empty() &&
          (std::isdigit(concrete_type[0]) || concrete_type[0] == '-'))
          concrete_type = "number";
      }
      std::string spec_name = cls_name + "__" + concrete_type;
      // Instantiate if not already done
      if(class_types.find(spec_name) == class_types.end())
      {
        // Extract type parameter name
        std::string tp_name = "T";
        const jsont &tp = json_member(gen_it->second, "typeParameters");
        if(tp.is_array() && !to_json_array(tp).empty())
        {
          const jsont &first_tp = *to_json_array(tp).begin();
          std::string n = json_string(json_member(first_tp, "_type"));
          if(!n.empty())
            tp_name = n;
        }
        // Temporarily remove from generic_classes, instantiate, restore
        jsont saved_node = gen_it->second;
        generic_classes.erase(gen_it);
        std::string saved_tp = current_generic_type_param;
        std::string saved_concrete = current_generic_concrete;
        current_generic_type_param = tp_name;
        current_generic_concrete = concrete_type;
        // Temporarily rename the class AST for specialization
        jsont modified = saved_node;
        // Modify name in place — need non-const access
        jsont &name_obj = const_cast<jsont &>(json_member(modified, "name"));
        if(name_obj.is_object())
        {
          auto &obj = const_cast<json_objectt &>(to_json_object(name_obj));
          obj["text"] = json_stringt{spec_name};
        }
        convert_statement(modified);
        current_generic_type_param = saved_tp;
        current_generic_concrete = saved_concrete;
        generic_classes[cls_name] = saved_node;
      }
      // Now treat as a regular class call with the specialized name
      auto spec_it = class_types.find(spec_name);
      if(spec_it != class_types.end())
      {
        static unsigned gnew_ctr = 0;
        std::string tmp_name =
          "__new_" + spec_name + "_" + std::to_string(gnew_ctr++);
        std::string tmp_qname =
          "typescript::" +
          (current_function.empty() ? "" : current_function + "::") + tmp_name;
        irep_idt tmp_id{tmp_qname};
        if(symbol_table.lookup(tmp_id) == nullptr)
        {
          symbolt ts{tmp_id, spec_it->second, "typescript"};
          ts.base_name = tmp_name;
          ts.is_lvalue = true;
          ts.is_state_var = true;
          symbol_table.add(ts);
        }
        symbol_exprt tmp = symbol_table.lookup_ref(tmp_id).symbol_expr();
        irep_idt ctor_id{"typescript::" + spec_name + "::__init__"};
        const symbolt *ctor = symbol_table.lookup(ctor_id);
        if(ctor != nullptr)
        {
          exprt::operandst args;
          args.push_back(address_of_exprt{tmp});
          const jsont &call_args = json_member(node, "arguments");
          if(call_args.is_array())
            for(const auto &a : to_json_array(call_args))
              args.push_back(convert_expression(a));
          const auto &params = to_code_type(ctor->type).parameters();
          for(std::size_t i = 0; i < args.size() && i < params.size(); i++)
            if(args[i].type() != params[i].type())
              args[i] = typecast_exprt(args[i], params[i].type());
          pending_stmts.push_back(
            code_expressiont{side_effect_expr_function_callt{
              ctor->symbol_expr(),
              std::move(args),
              empty_typet{},
              get_location(node)}});
        }
        return tmp;
      }
    }
    auto cls_it = class_types.find(cls_name);
    if(cls_it != class_types.end())
    {
      // Create temp object
      static unsigned new_ctr = 0;
      std::string tmp_name =
        "__new_" + cls_name + "_" + std::to_string(new_ctr++);
      std::string tmp_qname =
        "typescript::" +
        (current_function.empty() ? "" : current_function + "::") + tmp_name;
      irep_idt tmp_id{tmp_qname};
      if(symbol_table.lookup(tmp_id) == nullptr)
      {
        symbolt ts{tmp_id, cls_it->second, "typescript"};
        ts.base_name = tmp_name;
        ts.is_lvalue = true;
        ts.is_state_var = true;
        symbol_table.add(ts);
      }
      symbol_exprt tmp = symbol_table.lookup_ref(tmp_id).symbol_expr();
      // Call constructor
      irep_idt ctor_id{"typescript::" + cls_name + "::__init__"};
      const symbolt *ctor = symbol_table.lookup(ctor_id);
      if(ctor != nullptr)
      {
        exprt::operandst args;
        args.push_back(address_of_exprt{tmp});
        const jsont &call_args = json_member(node, "arguments");
        if(call_args.is_array())
          for(const auto &a : to_json_array(call_args))
            args.push_back(convert_expression(a));
        // Type-match args to params
        const auto &params = to_code_type(ctor->type).parameters();
        for(std::size_t i = 0; i < args.size() && i < params.size(); i++)
          if(args[i].type() != params[i].type())
            args[i] = typecast_exprt(args[i], params[i].type());
        pending_stmts.push_back(
          code_expressiont{side_effect_expr_function_callt{
            ctor->symbol_expr(),
            std::move(args),
            empty_typet{},
            get_location(node)}});
      }
      return tmp;
    }
    return nil_exprt{};
  }
  // ES2024 sec-template-literals
  // ES2024 §13.3.11: Tagged template expression.
  // tag`head${expr}tail` ≡ tag(["head","tail"], expr). For constant
  // interpolations we fold at conversion time (matching the existing
  // TemplateExpression path). Non-constant values fall back to nondet.
  if(kind == "TaggedTemplateExpression")
  {
    const jsont &tmpl = json_member(node, "template");
    std::vector<std::string> strings;
    std::vector<exprt> values;
    std::string tmpl_kind = json_string(json_member(tmpl, "_kind"));
    if(
      tmpl_kind == "NoSubstitutionTemplateLiteral" ||
      tmpl_kind == "FirstTemplateToken")
    {
      strings.push_back(json_string(json_member(tmpl, "text")));
    }
    else
    {
      strings.push_back(
        json_string(json_member(json_member(tmpl, "head"), "text")));
      const jsont &spans = json_member(tmpl, "templateSpans");
      if(spans.is_array())
        for(const auto &span : to_json_array(spans))
        {
          values.push_back(convert_expression(json_member(span, "expression")));
          strings.push_back(
            json_string(json_member(json_member(span, "literal"), "text")));
        }
    }
    bool all_const = true;
    std::string concatenated;
    for(std::size_t i = 0; i < strings.size(); ++i)
    {
      concatenated += strings[i];
      if(i < values.size())
      {
        exprt val = values[i];
        if(val.id() == ID_symbol)
        {
          const symbolt *vs =
            symbol_table.lookup(to_symbol_expr(val).get_identifier());
          if(vs && !vs->value.is_nil())
            val = vs->value;
        }
        if(val.is_constant() && val.type().id() == ID_floatbv)
        {
          ieee_floatt fv{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          fv.from_expr(to_constant_expr(val));
          ieee_floatt rounded = fv.round_to_integral();
          if(rounded == fv)
            concatenated += integer2string(fv.to_integer());
          else
            concatenated += fv.to_ansi_c_string();
        }
        else if(is_typescript_string_type(val.type()))
        {
          std::string sv = extract_string_value(val);
          if(!sv.empty())
            concatenated += sv.substr(2);
          else
            all_const = false;
        }
        else
          all_const = false;
      }
    }
    if(all_const)
      return convert_string_literal_from_text(concatenated);
    return side_effect_expr_nondett{
      typescript_string_type(), get_location(node)};
  }
  // TSH: Everyday Types > Template Literal Types
  if(kind == "TemplateExpression")
  {
    // Concatenate head + spans at conversion time
    std::string result;
    bool all_const = true;
    std::string head_text =
      json_string(json_member(json_member(node, "head"), "text"));
    result += head_text;
    // Recursive numeric-constant folder for template literal interpolation.
    // Handles constants, unary_minus, and binary arithmetic when all
    // leaves are constants.
    std::function<std::optional<double>(const exprt &)> fold_num =
      [&](const exprt &e) -> std::optional<double>
    {
      if(e.is_constant() && e.type().id() == ID_floatbv)
      {
        ieee_floatt fv{
          ieee_float_spect::double_precision(),
          ieee_floatt::rounding_modet::ROUND_TO_EVEN};
        fv.from_expr(to_constant_expr(e));
        if(fv.is_NaN() || fv.is_infinity())
          return std::nullopt;
        return std::stod(fv.to_ansi_c_string());
      }
      if(e.id() == ID_symbol)
      {
        const symbolt *ns =
          symbol_table.lookup(to_symbol_expr(e).get_identifier());
        if(ns && !ns->value.is_nil())
          return fold_num(ns->value);
        return std::nullopt;
      }
      if(e.id() == ID_unary_minus && e.operands().size() == 1)
      {
        auto v = fold_num(e.operands()[0]);
        if(v.has_value())
          return -v.value();
        return std::nullopt;
      }
      if(e.operands().size() >= 2)
      {
        auto l = fold_num(e.operands()[0]);
        auto r = fold_num(e.operands()[1]);
        if(!l.has_value() || !r.has_value())
          return std::nullopt;
        if(e.id() == ID_plus || e.id() == ID_floatbv_plus)
          return l.value() + r.value();
        if(e.id() == ID_minus || e.id() == ID_floatbv_minus)
          return l.value() - r.value();
        if(e.id() == ID_mult || e.id() == ID_floatbv_mult)
          return l.value() * r.value();
        if((e.id() == ID_div || e.id() == ID_floatbv_div) && r.value() != 0.0)
          return l.value() / r.value();
      }
      return std::nullopt;
    };
    auto num_to_text = [](double d) -> std::string
    {
      if(d == std::floor(d) && std::abs(d) < 1e15)
        return std::to_string(static_cast<long long>(d));
      std::ostringstream oss;
      oss << d;
      return oss.str();
    };
    const jsont &spans = json_member(node, "templateSpans");
    if(spans.is_array())
    {
      for(const auto &span : to_json_array(spans))
      {
        exprt expr = convert_expression(json_member(span, "expression"));
        // Try to extract constant string value
        std::string sv = extract_string_value(expr);
        if(!sv.empty())
          result += sv.substr(2);
        else if(expr.type().id() == ID_floatbv)
        {
          auto v = fold_num(expr);
          if(v.has_value())
            result += num_to_text(v.value());
          else
            all_const = false;
        }
        else if(expr.type().id() == ID_bool)
        {
          if(expr.is_constant())
            result += (expr.is_true() ? "true" : "false");
          else
            all_const = false;
        }
        else
          all_const = false;
        result +=
          json_string(json_member(json_member(span, "literal"), "text"));
      }
    }
    if(all_const)
      return convert_string_literal_from_text(result);
    return side_effect_expr_nondett{
      typescript_string_type(), get_location(node)};
  }
  // ES2024 sec-object-initializer
  if(kind == "ObjectLiteralExpression")
  {
    // Build struct from properties
    std::string ts_type = json_string(json_member(node, "_type"));
    const jsont &props = json_member(node, "properties");
    if(!props.is_array())
      return nil_exprt{};
    struct_typet::componentst components;
    exprt::operandst fields;
    for(const auto &prop : to_json_array(props))
    {
      std::string pk = json_string(json_member(prop, "_kind"));
      if(pk == "ShorthandPropertyAssignment")
      {
        // { x, y } — shorthand for { x: x, y: y }
        std::string pname =
          json_string(json_member(json_member(prop, "name"), "text"));
        if(!pname.empty())
        {
          exprt val = convert_identifier(json_member(prop, "name"));
          if(!val.is_nil())
          {
            components.push_back(struct_typet::componentt{pname, val.type()});
            fields.push_back(val);
          }
        }
        continue;
      }
      if(pk == "SpreadAssignment")
      {
        // { ...obj } — copy all fields from source object
        exprt src = convert_expression(json_member(prop, "expression"));
        if(src.id() == ID_symbol)
        {
          const symbolt *s =
            symbol_table.lookup(to_symbol_expr(src).get_identifier());
          if(s && !s->value.is_nil())
            src = s->value;
        }
        if(src.id() == ID_struct && src.type().id() == ID_struct)
        {
          const auto &st = to_struct_type(src.type());
          for(std::size_t i = 0;
              i < st.components().size() && i < src.operands().size();
              ++i)
          {
            // Override existing field if name matches
            std::string fname = id2string(st.components()[i].get_name());
            bool overridden = false;
            for(std::size_t ci = 0; ci < components.size(); ++ci)
            {
              if(id2string(components[ci].get_name()) == fname)
              {
                fields[ci] = src.operands()[i];
                overridden = true;
                break;
              }
            }
            if(!overridden)
            {
              components.push_back(st.components()[i]);
              fields.push_back(src.operands()[i]);
            }
          }
        }
        continue;
      }
      std::string pname =
        json_string(json_member(json_member(prop, "name"), "text"));
      exprt val = convert_expression(json_member(prop, "initializer"));
      if(val.is_nil())
        continue;
      // Check if field already exists (from spread) — override it
      bool found = false;
      for(std::size_t ci = 0; ci < components.size(); ++ci)
      {
        if(id2string(components[ci].get_name()) == pname)
        {
          fields[ci] = val;
          found = true;
          break;
        }
      }
      if(!found)
      {
        components.push_back(struct_typet::componentt{pname, val.type()});
        fields.push_back(val);
      }
    }
    // ES2024 optional-chaining support: when the source AST type is a
    // named interface type with MORE fields than we produced, fill the
    // missing fields with a NaN sentinel (for numbers) or with a
    // recursively-NaN-filled struct (for object-typed fields). This
    // lets later `o.x?.y` accesses see NaN as "undefined".
    if(!ts_type.empty())
    {
      auto make_nan = [](const typet &t) -> exprt
      {
        if(t.id() == ID_floatbv)
        {
          return ts_nan_with_payload(TS_NAN_PAYLOAD_UNDEFINED);
        }
        if(t.id() == ID_bool)
          return false_exprt{};
        if(t.id() == ID_signedbv || t.id() == ID_unsignedbv)
          return from_integer(0, t);
        return side_effect_expr_nondett{t, source_locationt{}};
      };
      std::function<exprt(const typet &)> default_value;
      default_value = [&](const typet &t) -> exprt
      {
        if(t.id() == ID_struct)
        {
          const auto &sub_st = to_struct_type(t);
          exprt::operandst sub_fields;
          for(const auto &c : sub_st.components())
            sub_fields.push_back(default_value(c.type()));
          return struct_exprt{std::move(sub_fields), sub_st};
        }
        return make_nan(t);
      };
      auto target_it = class_types.find(ts_type);
      if(target_it != class_types.end())
      {
        const auto &target_st = target_it->second;
        for(const auto &c : target_st.components())
        {
          std::string cname = id2string(c.get_name());
          bool already = false;
          for(const auto &ec : components)
            if(id2string(ec.get_name()) == cname)
            {
              already = true;
              break;
            }
          if(!already)
          {
            components.push_back(c);
            fields.push_back(default_value(c.type()));
          }
        }
      }
    }
    struct_typet st{components};
    return struct_exprt{std::move(fields), st};
  }
  // ES2024 sec-array-initializer
  if(kind == "ArrayLiteralExpression")
  {
    const jsont &elts = json_member(node, "elements");
    if(!elts.is_array())
      return nil_exprt{};
    if(to_json_array(elts).empty())
    {
      // Empty array: {length: 0, data: [0, 0, ...]}
      typet elem_type = double_type();
      // Try to get element type from _type annotation
      std::string arr_type_str = json_string(json_member(node, "_type"));
      if(arr_type_str.size() > 2 && arr_type_str.back() == ']')
      {
        std::string et = arr_type_str.substr(0, arr_type_str.size() - 2);
        elem_type = convert_type(et);
      }
      std::size_t max_len = TYPESCRIPT_MAX_ARRAY_LENGTH;
      exprt::operandst zeros;
      for(std::size_t i = 0; i < max_len; ++i)
        zeros.push_back(from_integer(0, elem_type));
      array_typet at{elem_type, from_integer(max_len, signedbv_typet{64})};
      struct_typet lt;
      lt.components().push_back(
        struct_typet::componentt{"length", signedbv_typet{64}});
      lt.components().push_back(struct_typet::componentt{"data", at});
      lt.set_tag("typescript_array");
      return struct_exprt{
        {from_integer(0, signedbv_typet{64}),
         array_exprt{std::move(zeros), at}},
        lt};
    }
    exprt::operandst elements;
    typet elem_type = double_type();     // default
    exprt actual_len_expr = nil_exprt{}; // set by spread of runtime arrays
    for(const auto &elt : to_json_array(elts))
    {
      std::string elt_kind = json_string(json_member(elt, "_kind"));
      if(elt_kind == "SpreadElement")
      {
        // Expand spread: copy elements from source array
        exprt src = convert_expression(json_member(elt, "expression"));
        // Resolve symbol to its value
        if(src.id() == ID_symbol)
        {
          const symbolt *s =
            symbol_table.lookup(to_symbol_expr(src).get_identifier());
          if(s && !s->value.is_nil())
            src = s->value;
        }
        if(!src.is_nil() && src.id() == ID_struct && src.operands().size() >= 2)
        {
          const exprt &data = src.operands()[1];
          mp_integer len{0};
          if(src.operands()[0].is_constant())
            to_integer(to_constant_expr(src.operands()[0]), len);
          for(mp_integer i = 0; i < len; ++i)
          {
            auto idx = i.to_ulong();
            if(idx < data.operands().size())
            {
              elem_type = data.operands()[idx].type();
              elements.push_back(data.operands()[idx]);
            }
          }
        }
        else if(
          !src.is_nil() && src.type().id() == ID_struct &&
          to_struct_type(src.type()).get_tag() == "typescript_array")
        {
          // Parameter or runtime array: copy via member access
          const auto &st = to_struct_type(src.type());
          exprt len_expr = member_exprt{src, "length", signedbv_typet{64}};
          exprt data_expr =
            member_exprt{src, "data", st.get_component("data").type()};
          elem_type =
            to_array_type(st.get_component("data").type()).element_type();
          std::size_t max_len = TYPESCRIPT_MAX_ARRAY_LENGTH;
          for(std::size_t i = 0; i < max_len; ++i)
            elements.push_back(
              index_exprt{data_expr, from_integer(i, signedbv_typet{64})});
          // Use the source length for the result
          actual_len_expr = len_expr;
        }
      }
      else
      {
        exprt val = convert_expression(elt);
        if(!val.is_nil())
        {
          elem_type = val.type();
          elements.push_back(val);
        }
      }
    }
    std::size_t actual_len = elements.size();
    // Heterogeneous-tuple detection: if _type is a tuple AND the
    // element types differ, build a typescript_tuple struct. For
    // homogeneous tuples (all same type), fall through to the
    // regular array-struct to preserve compatibility with existing
    // destructure and iteration code.
    {
      std::string arr_type_str = json_string(json_member(node, "_type"));
      bool looks_like_tuple = !arr_type_str.empty() &&
                              arr_type_str.front() == '[' &&
                              arr_type_str.back() == ']';
      bool heterogeneous = false;
      if(!elements.empty())
      {
        typet first = elements[0].type();
        for(std::size_t i = 1; i < elements.size(); i++)
          if(elements[i].type() != first)
          {
            heterogeneous = true;
            break;
          }
      }
      if(looks_like_tuple && heterogeneous)
      {
        struct_typet tuple_st;
        for(std::size_t i = 0; i < elements.size(); i++)
          tuple_st.components().push_back(struct_typet::componentt{
            "_" + std::to_string(i), elements[i].type()});
        tuple_st.set_tag("typescript_tuple");
        return struct_exprt{std::move(elements), tuple_st};
      }
    }
    // Build list struct { length, data[] }
    std::size_t max_len = TYPESCRIPT_MAX_ARRAY_LENGTH;
    while(elements.size() < max_len)
      elements.push_back(from_integer(0, elem_type));
    array_typet arr_type{elem_type, from_integer(max_len, signedbv_typet{64})};
    struct_typet list_type = make_array_struct_type(arr_type);
    exprt len_val = actual_len_expr.is_nil()
                      ? exprt{from_integer(actual_len, signedbv_typet{64})}
                      : actual_len_expr;
    return struct_exprt{
      {len_val, array_exprt{std::move(elements), arr_type}}, list_type};
  }
  // ES2024 sec-typeof-operator
  // TSH: Narrowing > typeof type guards
  if(kind == "TypeOfExpression")
  {
    exprt operand = convert_expression(json_member(node, "expression"));
    std::string ts_type =
      json_string(json_member(json_member(node, "expression"), "_type"));
    // For union types, return tag field for runtime typeof check
    if(
      !operand.is_nil() && operand.type().id() == ID_struct &&
      to_struct_type(operand.type()).get_tag() == "typescript_union")
    {
      return member_exprt{operand, "__tag", signedbv_typet{32}};
    }
    // ES2024 sec-typeof-operator — map the operand to a runtime type.
    // TypeScript often reports literal types (e.g. "42", "\"hello\"",
    // "true") for constant expressions. We normalize these to the
    // runtime type tags required by the spec.
    std::string typeof_result = "object";
    auto is_numeric_literal = [](const std::string &t)
    {
      if(t.empty())
        return false;
      for(size_t i = 0; i < t.size(); i++)
      {
        char c = t[i];
        if(!(std::isdigit(static_cast<unsigned char>(c)) || c == '-' ||
             c == '+' || c == '.' || c == 'e' || c == 'E'))
          return false;
      }
      return true;
    };
    auto is_string_literal = [](const std::string &t)
    {
      // TypeScript reports string literal types as `"foo"` with quotes.
      return t.size() >= 2 && t.front() == '"' && t.back() == '"';
    };
    if(ts_type == "number" || is_numeric_literal(ts_type))
      typeof_result = "number";
    else if(ts_type == "string" || is_string_literal(ts_type))
      typeof_result = "string";
    else if(ts_type == "boolean" || ts_type == "true" || ts_type == "false")
      typeof_result = "boolean";
    else if(ts_type == "undefined")
      typeof_result = "undefined";
    else if(ts_type == "null")
      typeof_result = "object"; // ES2024 quirk: typeof null === "object"
    else if(ts_type.find("=>") != std::string::npos)
      typeof_result = "function";
    else if(
      !operand.is_nil() &&
      (operand.type().id() == ID_pointer || operand.type().id() == ID_code))
      typeof_result = "function";
    else if(!operand.is_nil() && operand.type().id() == ID_floatbv)
      typeof_result = "number"; // fallback for numeric operands
    else if(!operand.is_nil() && is_typescript_string_type(operand.type()))
      typeof_result = "string"; // fallback for string operands
    else if(!operand.is_nil() && operand.type().id() == ID_bool)
      typeof_result = "boolean"; // fallback for bool operands
    return convert_string_literal_from_text(typeof_result);
  }
  // ES2024 sec-void-operator
  if(kind == "VoidExpression")
  {
    convert_expression(
      json_member(node, "expression"));         // evaluate for side effects
    return from_integer(0, signedbv_typet{64}); // undefined
  }
  // ES2024 sec-delete-operator
  if(kind == "DeleteExpression")
    return true_exprt{}; // always succeeds in our model
  // ES2024 sec-await: AwaitExpression — model as identity (sequential)
  if(kind == "AwaitExpression" || kind == "NonNullExpression")
    return convert_expression(json_member(node, "expression"));
  // TSH: Type Assertions — x as T
  if(kind == "AsExpression" || kind == "TypeAssertionExpression")
  {
    exprt inner = convert_expression(json_member(node, "expression"));
    std::string target_type = json_string(json_member(node, "_type"));
    typet tt = convert_type(target_type);
    if(!inner.is_nil() && inner.type() != tt && tt.id() != ID_empty)
      return typecast_exprt{inner, tt};
    return inner;
  }
  // ES2024 sec-spread-element
  if(kind == "SpreadElement")
    return convert_expression(json_member(node, "expression"));
  log.warning() << "Unsupported expression: " << kind
                << " (not yet implemented in TypeScript frontend); "
                << "returning nondet" << messaget::eom;
  // Return nondet instead of nil for graceful degradation
  std::string ts_type = json_string(json_member(node, "_type"));
  if(!ts_type.empty())
  {
    typet t = convert_type(ts_type);
    if(t.id() != ID_empty)
      return side_effect_expr_nondett{t, get_location(node)};
  }
  return side_effect_expr_nondett{double_type(), get_location(node)};
}

// ES2024 sec-ecmascript-language-types-number-type
exprt typescript_convertert::convert_numeric_literal(const jsont &node)
{
  std::string text = json_string(json_member(node, "text"));
  if(text.empty())
    return from_integer(0, double_type());
  double val = std::stod(text);
  // Convert double to IEEE 754 bit representation
  uint64_t bits;
  static_assert(sizeof(double) == sizeof(uint64_t), "double must be 64 bits");
  std::memcpy(&bits, &val, sizeof(bits));
  // Use string conversion to avoid mp_integer constructor issues
  std::string bits_str = std::to_string(bits);
  return constant_exprt{
    integer2bvrep(mp_integer{bits_str.c_str()}, 64), double_type()};
}

// ES2024 §7.1.2 ToBoolean coercion.
// - undefined, null, NaN, 0, -0, "", false → false
// - everything else → true
// For a typescript string struct, truthy iff length > 0. For bool,
// return as-is. For numeric, typecast (0 → false is correct).
exprt typescript_convertert::ts_to_boolean(const exprt &e)
{
  if(e.type().id() == ID_bool)
    return e;
  if(is_typescript_string_type(e.type()))
  {
    // Build `e.length != 0` (length is signedbv[32]).
    exprt len = member_exprt{e, "length", signedbv_typet{32}};
    return notequal_exprt{len, from_integer(0, signedbv_typet{32})};
  }
  // Numeric (floatbv / signedbv / unsignedbv) → typecast; non-zero is
  // truthy. This is how CBMC's C frontend handles `if (x)` with int.
  return typecast_exprt{e, bool_typet{}};
}

std::string typescript_convertert::extract_string_value(const exprt &e)
{
  // Check string_constants map
  if(e.id() == ID_symbol)
  {
    auto it = string_constants.find(to_symbol_expr(e).get_identifier());
    if(it != string_constants.end())
      return "S:" + it->second;
    // Fallback: check symbol table value
    const symbolt *s = symbol_table.lookup(to_symbol_expr(e).get_identifier());
    if(s && !s->value.is_nil())
      return extract_string_value(s->value);
  }
  // Handle member expressions: obj.field
  if(e.id() == ID_member)
  {
    const auto &me = to_member_expr(e);
    const exprt &compound = me.compound();
    // Try to resolve the compound to its value
    if(compound.id() == ID_symbol)
    {
      const symbolt *s =
        symbol_table.lookup(to_symbol_expr(compound).get_identifier());
      if(s && !s->value.is_nil() && s->value.id() == ID_struct)
      {
        // Find the field in the struct
        if(s->value.type().id() == ID_struct)
        {
          const auto &st = to_struct_type(s->value.type());
          for(std::size_t i = 0; i < st.components().size(); ++i)
          {
            if(
              st.components()[i].get_name() == me.get_component_name() &&
              i < s->value.operands().size())
            {
              return extract_string_value(s->value.operands()[i]);
            }
          }
        }
      }
    }
  }
  // Check struct{length, data[]} format (inline array)
  if(
    e.id() == ID_struct && e.operands().size() >= 2 &&
    e.operands()[0].is_constant())
  {
    mp_integer len;
    if(!to_integer(to_constant_expr(e.operands()[0]), len))
    {
      const exprt &data = e.operands()[1];
      if(data.id() == ID_array)
      {
        std::string s;
        bool all_const = true;
        for(mp_integer i = 0; i < len; ++i)
        {
          auto idx = i.to_ulong();
          if(idx < data.operands().size() && data.operands()[idx].is_constant())
          {
            mp_integer ch;
            if(!to_integer(to_constant_expr(data.operands()[idx]), ch))
              s += static_cast<char>(ch.to_ulong());
          }
          else
          {
            all_const = false;
            break;
          }
        }
        if(all_const)
          return "S:" + s;
      }
    }
  }
  return "";
}

exprt typescript_convertert::ts_string_to_refined(const exprt &ts_string)
{
  // Boundary conversion from our inline-array string struct to a
  // refined_string_exprt the solver can consume.
  //
  // Key subtlety (SOUNDNESS-CRITICAL): the solver tracks an array's
  // length via array_pool's length_of_array map. That map is
  // populated by attempt_assign_length_from_type, which reads the
  // array type's STATIC size. Our inline string uses
  // char[TYPESCRIPT_MAX_STRING_LENGTH] (64), so naively associating
  // the inline array would tell the solver every string has length
  // 64 — and then associate_length_to_array would add the UNSAT
  // constraint `64 == dynamic_length`, silently making every path
  // vacuously pass. (This bug shipped briefly: any regression test
  // exercising a symbolic receiver + refined-string method was
  // vacuously succeeding.)
  //
  // Fix: we materialise an INFINITY-sized array symbol for the
  // solver's view, copy the characters from the inline struct into
  // it, and associate the length separately. With infinity size,
  // attempt_assign_length_from_type allocates a fresh length symbol,
  // and our associate_length_to_array constrains that symbol to the
  // dynamic string length. The content loop makes the solver's
  // array agree with the inline array on all in-range positions.

  typet our_len_type = signedbv_typet{32};
  typet solver_len_type = signedbv_typet{64};
  typet char_type = unsignedbv_typet{16};
  array_typet inline_array_type{
    char_type, from_integer(TYPESCRIPT_MAX_STRING_LENGTH, solver_len_type)};
  pointer_typet char_ptr_type{char_type, 64};

  static unsigned str_tmp_ctr = 0;
  auto fresh = [this](const std::string &base, const typet &t)
  {
    std::string name = base + "_" + std::to_string(str_tmp_ctr++);
    irep_idt id{"typescript::" + name};
    symbolt s{id, t, "typescript"};
    s.base_name = name;
    s.is_lvalue = true;
    s.is_state_var = true;
    symbol_table.add(s);
    return symbol_table.lookup_ref(id).symbol_expr();
  };

  // Extract dynamic length as an int64 scalar.
  exprt temp_len = fresh("__ts_str_len", solver_len_type);
  pending_stmts.push_back(code_frontend_assignt{
    temp_len,
    typecast_exprt{
      member_exprt{ts_string, "length", our_len_type}, solver_len_type}});

  // Create an infinity-sized array symbol. Copy the inline data into
  // it via a SINGLE assignment built from a with_exprt chain (one
  // position updated per with layer). This yields exactly one SSA
  // assignment for the whole array, avoiding the 64-fold blowup from
  // per-position assignments. For each position i in [0, MAX), we
  // override that position in the symbolic-sized array with
  //   i < len ? inline_data[i] : 0
  // but since in-bounds positions hold the content and out-of-bounds
  // are logically zero (they don't affect the solver because the
  // refined_string length caps access at `len`), we can simply copy
  // all MAX positions unconditionally.
  array_typet infty_type{char_type, infinity_exprt(solver_len_type)};
  exprt solver_arr = fresh("__ts_str_sarr", infty_type);

  exprt data_member = member_exprt{ts_string, "data", inline_array_type};
  exprt arr_val = side_effect_expr_nondett{infty_type, source_locationt{}};
  for(std::size_t i = 0; i < TYPESCRIPT_MAX_STRING_LENGTH; ++i)
  {
    exprt idx = from_integer(i, solver_len_type);
    exprt val = index_exprt{data_member, idx, char_type};
    arr_val = with_exprt{arr_val, idx, val};
  }
  pending_stmts.push_back(code_frontend_assignt{solver_arr, arr_val});

  // Allocate the pointer on the heap via ID_allocate. This is
  // crucial for call-site uniqueness when ts_string_to_refined
  // executes inside a helper function that the user program calls
  // more than once: address_of(solver_arr[0]) is a storage-based
  // expression whose value is the same across all invocations of
  // the same helper (address_of does not change with SSA version).
  // If two call sites emit the same pointer, the solver's
  // array_pool links them to the first-registered array, producing
  // stale axioms for subsequent calls. A fresh allocation per call
  // site breaks this tie. We also assign solver_arr's content into
  // the dereference so the solver's char-array-for-pointer lookup
  // sees our actual data.
  exprt pointer = fresh("__ts_str_ptr", char_ptr_type);
  pending_stmts.push_back(code_frontend_assignt{
    pointer,
    side_effect_exprt{
      ID_allocate,
      {from_integer(TYPESCRIPT_MAX_STRING_LENGTH * 2, size_type()),
       false_exprt{}},
      char_ptr_type,
      source_locationt{}}});

  // Declare and emit associate calls.
  auto declare_assoc =
    [this](const irep_idt &name, const typet &arg1_t, const typet &arg2_t)
  {
    if(symbol_table.lookup(name) == nullptr)
    {
      std::vector<typet> arg_types = {arg1_t, arg2_t};
      mathematical_function_typet ft(std::move(arg_types), signedbv_typet{32});
      symbolt fs{name, ft, "typescript"};
      fs.base_name = id2string(name);
      symbol_table.add(fs);
    }
  };
  declare_assoc(
    ID_cprover_associate_array_to_pointer_func, infty_type, char_ptr_type);
  declare_assoc(
    ID_cprover_associate_length_to_array_func, infty_type, solver_len_type);

  auto emit_assoc = [&](const irep_idt &func, const exprt &a, const exprt &b)
  {
    exprt rc = fresh("__ts_str_assoc_rc", signedbv_typet{32});
    function_application_exprt app(
      symbol_exprt{func, symbol_table.lookup_ref(func).type}, {a, b});
    app.type() = signedbv_typet{32};
    pending_stmts.push_back(code_frontend_assignt{rc, app});
  };

  emit_assoc(ID_cprover_associate_array_to_pointer_func, solver_arr, pointer);
  emit_assoc(ID_cprover_associate_length_to_array_func, solver_arr, temp_len);

  refined_string_typet refined_ty{solver_len_type, char_ptr_type};
  return struct_exprt{{temp_len, pointer}, refined_ty};
}

exprt typescript_convertert::ts_call_string_returning_function(
  const irep_idt &func_id,
  const exprt::operandst &args)
{
  // Allocate fresh scalar temps for the result length and content,
  // then call the cprover_string_*_func with
  //   (result_length, result_content, args...)
  // and unpack back into our inline-array struct via per-slot
  // cprover_string_char_at_func applications.
  typet solver_len_type = signedbv_typet{64};
  typet char_type = unsignedbv_typet{16};
  pointer_typet char_ptr_type{char_type, 64};
  refined_string_typet refined_ty{solver_len_type, char_ptr_type};

  static unsigned ret_ctr = 0;
  auto fresh = [this](const std::string &base, const typet &t)
  {
    std::string name = base + "_" + std::to_string(ret_ctr++);
    irep_idt id{"typescript::" + name};
    symbolt s{id, t, "typescript"};
    s.base_name = name;
    s.is_lvalue = true;
    s.is_state_var = true;
    symbol_table.add(s);
    return symbol_table.lookup_ref(id).symbol_expr();
  };
  exprt result_len_sym = fresh("__ts_strfn_len", solver_len_type);

  // Seed the result-length symbol with a nondet value. Without an
  // assignment in the SSA the symbol is only read (as the first
  // argument to the solver function and in the per-slot readout
  // below), which means if the same
  // ts_call_string_returning_function body is reused across
  // multiple call sites (e.g. a user-defined helper that chains
  // string operations and is called more than once), all call
  // sites share the same SSA version #0 of this free variable.
  // The solver's axioms from one call then contradict the axioms
  // from another on the same variable, producing VERIFICATION
  // FAILED even when each call in isolation would succeed. Writing
  // the symbol (nondet → new SSA version per call site) gives each
  // call independent solver reasoning.
  pending_stmts.push_back(code_frontend_assignt{
    result_len_sym,
    side_effect_expr_nondett{solver_len_type, source_locationt{}}});

  // Allocate the result buffer on the heap via ID_allocate. This is
  // the crucial bit for multi-call correctness: each execution of
  // this function body (one per call site of a string-returning
  // helper in the user program) produces a unique dynamic object,
  // so the solver's array_pool keys (which are pointer expressions
  // simplified to their allocated addresses) are distinct across
  // call sites. Without per-call allocation, result_ptr_sym =
  // address_of(res_arr_symbol[0]) resolves to the SAME underlying
  // storage across call sites (address_of tracks the variable's
  // storage, independent of SSA version), which made array_pool
  // reject the second-call association via the idempotent-insert
  // path and leave the second call's axioms targeting the first
  // call's array. See regression
  // string-concat-chained-in-function.
  //
  // We allocate enough bytes for TYPESCRIPT_MAX_STRING_LENGTH UTF-16
  // characters (2 bytes each). The solver only uses
  // result_ptr_sym as an opaque key plus the declared length, so
  // the exact size is just an over-approximation bound.
  array_typet infty_arr_type{char_type, infinity_exprt(solver_len_type)};
  exprt result_ptr_sym = fresh("__ts_strfn_ptr", char_ptr_type);
  pending_stmts.push_back(code_frontend_assignt{
    result_ptr_sym,
    side_effect_exprt{
      ID_allocate,
      {from_integer(TYPESCRIPT_MAX_STRING_LENGTH * 2, size_type()),
       false_exprt{}},
      char_ptr_type,
      source_locationt{}}});

  // Create a companion array symbol to hold the solver's view of
  // the allocated buffer's content. We associate this array with
  // the allocated pointer so the solver reads/writes our res_arr
  // rather than inventing its own fresh char_array_*. The array
  // needs a nondet seed for the same SSA-versioning reason as
  // result_len_sym above.
  exprt res_arr = fresh("__ts_strfn_arr", infty_arr_type);
  pending_stmts.push_back(code_frontend_assignt{
    res_arr, side_effect_expr_nondett{infty_arr_type, source_locationt{}}});

  auto declare_assoc2 =
    [this](const irep_idt &name, const typet &a1, const typet &a2)
  {
    if(symbol_table.lookup(name) == nullptr)
    {
      std::vector<typet> at = {a1, a2};
      mathematical_function_typet ft(std::move(at), signedbv_typet{32});
      symbolt fs{name, ft, "typescript"};
      fs.base_name = id2string(name);
      symbol_table.add(fs);
    }
  };
  declare_assoc2(
    ID_cprover_associate_array_to_pointer_func, infty_arr_type, char_ptr_type);
  declare_assoc2(
    ID_cprover_associate_length_to_array_func, infty_arr_type, solver_len_type);

  auto emit_assoc2 = [&](const irep_idt &func, const exprt &a, const exprt &b)
  {
    exprt rc2 = fresh("__ts_strfn_assoc_rc", signedbv_typet{32});
    function_application_exprt app2(
      symbol_exprt{func, symbol_table.lookup_ref(func).type}, {a, b});
    app2.type() = signedbv_typet{32};
    pending_stmts.push_back(code_frontend_assignt{rc2, app2});
  };
  emit_assoc2(
    ID_cprover_associate_array_to_pointer_func, res_arr, result_ptr_sym);
  emit_assoc2(
    ID_cprover_associate_length_to_array_func, res_arr, result_len_sym);

  // Declare the function if not already in the symbol table.
  if(symbol_table.lookup(func_id) == nullptr)
  {
    std::vector<typet> arg_types = {solver_len_type, char_ptr_type};
    for(const auto &a : args)
      arg_types.push_back(a.type());
    mathematical_function_typet ft(std::move(arg_types), signedbv_typet{32});
    symbolt fs{func_id, ft, "typescript"};
    fs.base_name = id2string(func_id);
    symbol_table.add(fs);
  }

  // Build the call: func(result_len, result_ptr, args...).
  exprt::operandst call_args;
  call_args.push_back(result_len_sym);
  call_args.push_back(result_ptr_sym);
  for(const auto &a : args)
    call_args.push_back(a);
  function_application_exprt app(
    symbol_exprt{func_id, symbol_table.lookup_ref(func_id).type}, call_args);
  app.type() = signedbv_typet{32};
  exprt rc_sym = fresh("__ts_strfn_rc", signedbv_typet{32});
  pending_stmts.push_back(code_frontend_assignt{rc_sym, app});

  // Declare cprover_string_char_at_func if needed (used by other
  // paths, e.g. charCodeAt on symbolic receivers).
  if(symbol_table.lookup(ID_cprover_string_char_at_func) == nullptr)
  {
    std::vector<typet> ca_arg_types = {refined_ty, solver_len_type};
    mathematical_function_typet ca_ft(std::move(ca_arg_types), char_type);
    symbolt cas{ID_cprover_string_char_at_func, ca_ft, "typescript"};
    cas.base_name = id2string(ID_cprover_string_char_at_func);
    symbol_table.add(cas);
  }

  // Build our inline-array result by reading per-character from
  // the refined-string result via cprover_string_char_at_func. This
  // is the idiomatic way to extract characters from a solver-
  // produced refined string: the char_at_func returns the i-th
  // character of the refined string, and the solver constrains it
  // against the same axioms it uses for the string function's
  // output. A direct index_exprt into res_arr fails because the
  // solver's refinement loop needs char_at_func calls to populate
  // its index set (the concrete indices at which it instantiates
  // universal quantifiers during model recovery) — without them
  // the loop reports "current index set is empty, this should not
  // happen" and the assertion returns ERROR.
  exprt refined_result =
    struct_exprt{{result_len_sym, result_ptr_sym}, refined_ty};
  struct_typet str_type = typescript_string_type();
  const auto &data_arr_type = to_array_type(str_type.components()[1].type());
  exprt result_len_32 = typecast_exprt{result_len_sym, signedbv_typet{32}};
  exprt::operandst result_chars;
  for(std::size_t i = 0; i < TYPESCRIPT_MAX_STRING_LENGTH; ++i)
  {
    exprt idx = from_integer(i, solver_len_type);
    function_application_exprt char_at_app(
      symbol_exprt{
        ID_cprover_string_char_at_func,
        symbol_table.lookup_ref(ID_cprover_string_char_at_func).type},
      {refined_result, idx});
    char_at_app.type() = char_type;
    exprt in_bounds = binary_relation_exprt{idx, ID_lt, result_len_sym};
    result_chars.push_back(
      if_exprt{in_bounds, std::move(char_at_app), from_integer(0, char_type)});
  }
  return struct_exprt{
    {result_len_32, array_exprt{std::move(result_chars), data_arr_type}},
    str_type};
}

exprt typescript_convertert::convert_string_literal_from_text(
  const std::string &text)
{
  // Build string as struct{length: signedbv[32], data: unsignedbv[16][MAX]}
  struct_typet str_type = typescript_string_type();
  const auto &data_type = to_array_type(str_type.components()[1].type());

  // Decode UTF-8 input → UTF-16 code units. The raw `text` is how
  // the TS compiler returns it (typically UTF-8). We emit one
  // code unit per unicode BMP codepoint, or a surrogate pair for
  // astral characters (codepoint > U+FFFF), matching the
  // ES2024 §6.1.4 String type.
  std::vector<uint16_t> code_units;
  for(std::size_t i = 0; i < text.size();)
  {
    unsigned char c = static_cast<unsigned char>(text[i]);
    uint32_t cp = 0;
    std::size_t n = 1;
    if((c & 0x80u) == 0)
    {
      cp = c;
    }
    else if((c & 0xE0u) == 0xC0u && i + 1 < text.size())
    {
      cp = (c & 0x1Fu);
      cp = (cp << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3Fu);
      n = 2;
    }
    else if((c & 0xF0u) == 0xE0u && i + 2 < text.size())
    {
      cp = (c & 0x0Fu);
      cp = (cp << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3Fu);
      cp = (cp << 6) | (static_cast<unsigned char>(text[i + 2]) & 0x3Fu);
      n = 3;
    }
    else if((c & 0xF8u) == 0xF0u && i + 3 < text.size())
    {
      cp = (c & 0x07u);
      cp = (cp << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3Fu);
      cp = (cp << 6) | (static_cast<unsigned char>(text[i + 2]) & 0x3Fu);
      cp = (cp << 6) | (static_cast<unsigned char>(text[i + 3]) & 0x3Fu);
      n = 4;
    }
    else
    {
      // Malformed UTF-8 (or latin-1 byte): emit as-is.
      cp = c;
    }
    if(cp > 0xFFFF)
    {
      // Emit surrogate pair.
      cp -= 0x10000;
      code_units.push_back(static_cast<uint16_t>(0xD800 | (cp >> 10)));
      code_units.push_back(static_cast<uint16_t>(0xDC00 | (cp & 0x3FF)));
    }
    else
    {
      code_units.push_back(static_cast<uint16_t>(cp));
    }
    i += n;
  }

  exprt::operandst chars;
  for(uint16_t u : code_units)
    chars.push_back(from_integer(u, unsignedbv_typet{16}));
  while(chars.size() < TYPESCRIPT_MAX_STRING_LENGTH)
    chars.push_back(from_integer(0, unsignedbv_typet{16}));

  exprt length =
    from_integer(static_cast<int>(code_units.size()), signedbv_typet{32});

  return struct_exprt{
    {length, array_exprt{std::move(chars), data_type}}, str_type};
}

exprt typescript_convertert::convert_string_literal(const jsont &node)
{
  return convert_string_literal_from_text(
    json_string(json_member(node, "text")));
}

exprt typescript_convertert::convert_boolean_literal(const jsont &node)
{
  std::string kind = json_string(json_member(node, "_kind"));
  return kind == "TrueKeyword" ? exprt{true_exprt{}} : exprt{false_exprt{}};
}

exprt typescript_convertert::convert_identifier(const jsont &node)
{
  std::string name = json_string(json_member(node, "text"));

  // Special identifiers
  // ES2024 sec-undefined: the `undefined` literal.
  // We model `undefined` as IEEE-754 NaN for numeric contexts. This
  // gives the nullish-coalescing operator (??) and optional chaining
  // (?.) a runtime check they can use. Soundness trade-off: in JS,
  // `NaN ?? x` returns NaN, but our model returns x. Real code
  // rarely uses NaN as a legitimate value while also wanting to
  // preserve it through ??, so this is an acceptable approximation.
  if(name == "undefined")
  {
    return ts_nan_with_payload(TS_NAN_PAYLOAD_UNDEFINED);
  }
  if(name == "NaN")
  {
    return ts_nan_with_payload(TS_NAN_PAYLOAD_REAL);
  }
  if(name == "Infinity")
  {
    ieee_floatt inf{
      ieee_float_spect::double_precision(),
      ieee_floatt::rounding_modet::ROUND_TO_EVEN};
    inf.make_plus_infinity();
    return inf.to_expr();
  }

  // Look up in symbol table
  std::string qualified =
    "typescript::" + (current_function.empty() ? "" : current_function + "::") +
    name;
  const symbolt *sym = symbol_table.lookup(irep_idt{qualified});
  if(sym != nullptr)
  {
    // Union type narrowing: if symbol is union but _type says specific type
    if(
      sym->type.id() == ID_struct &&
      to_struct_type(sym->type).get_tag() == "typescript_union")
    {
      std::string ann = json_string(json_member(node, "_type"));
      if(!ann.empty() && ann.find(" | ") == std::string::npos)
      {
        typet target = convert_type(ann);
        const auto &ust = to_struct_type(sym->type);
        for(std::size_t c = 1; c < ust.components().size(); ++c)
        {
          if(ust.components()[c].type() == target)
            return member_exprt{
              sym->symbol_expr(), ust.components()[c].get_name(), target};
        }
      }
    }
    return sym->symbol_expr();
  }

  // Try enclosing function scope (for closures accessing outer locals)
  if(!current_function.empty())
  {
    // Check captured_var_map to find the enclosing scope
    irep_idt fid{"typescript::" + current_function};
    auto cv_it = captured_var_map.find(fid);
    if(cv_it != captured_var_map.end())
    {
      for(const auto &[cv_name, cv_outer_id] : cv_it->second)
      {
        if(cv_name == name)
        {
          const symbolt *outer = symbol_table.lookup(cv_outer_id);
          if(outer != nullptr)
            return outer->symbol_expr();
        }
      }
    }
  }

  // Try module scope
  std::string global = "typescript::" + name;
  sym = symbol_table.lookup(irep_idt{global});
  if(sym != nullptr)
    return sym->symbol_expr();

  log.warning() << "Unknown identifier: '" << name
                << "' (likely a missing import or module-scoped symbol); "
                << "returning nondet" << messaget::eom;
  return side_effect_expr_nondett{double_type(), get_location(node)};
}

// ES2024 sec-applystringornumericbinaryoperator
exprt typescript_convertert::convert_binary_expression(const jsont &node)
{
  exprt left = convert_expression(json_member(node, "left"));
  exprt right = convert_expression(json_member(node, "right"));
  std::string op = json_string(json_member(node, "operator"));

  if(left.is_nil() || right.is_nil())
    return nil_exprt{};

  // ES2024 §21.4.4.45: Date objects coerce to their time value
  // (ToPrimitive → valueOf → getTime) in arithmetic and comparison
  // contexts. Extract the time field when a Date struct appears as
  // an operand of -, <, >, <=, >=.
  if(is_typescript_date_type(left.type()))
    left = member_exprt{left, "time", double_type()};
  if(is_typescript_date_type(right.type()))
    right = member_exprt{right, "time", double_type()};

  // Type promotion: ensure both sides have the same type
  // (skip for === and !== which handle type mismatches themselves)
  // (skip for PlusToken when one side is a string — the string concat
  //  branch coerces numbers/booleans to strings, not the other way around)
  bool plus_with_string =
    op == "PlusToken" && (is_typescript_string_type(left.type()) ||
                          is_typescript_string_type(right.type()));
  if(
    left.type() != right.type() && op != "EqualsEqualsEqualsToken" &&
    op != "ExclamationEqualsEqualsToken" && op != "InKeyword" &&
    !plus_with_string)
  {
    // Don't promote bigint to float or vice versa — mixed
    // bigint/number is a TypeError per ES2024 §6.1.6.2.
    if(
      is_typescript_bigint_type(left.type()) ||
      is_typescript_bigint_type(right.type()))
    {
      // If one side is bigint and the other isn't, this is a
      // spec violation. Emit a type-error assertion.
      if(left.type() != right.type())
      {
        // For now, just cast the non-bigint side to bigint
        // (over-approximation; real code would throw TypeError).
        if(is_typescript_bigint_type(left.type()))
          right = typecast_exprt{right, left.type()};
        else
          left = typecast_exprt{left, right.type()};
      }
    }
    else if(left.type().id() == ID_floatbv)
      right = typecast_exprt{right, left.type()};
    else if(right.type().id() == ID_floatbv)
      left = typecast_exprt{left, right.type()};
  }

  // ES2024 sec-addition-operator-plus
  if(op == "PlusToken")
  {
    // ES2024 §6.1.6.2: BigInt arithmetic — uses integer ops, no
    // rounding mode. Both operands must be bigint (mixed
    // bigint/number is a TypeError per spec).
    if(
      is_typescript_bigint_type(left.type()) &&
      is_typescript_bigint_type(right.type()))
      return plus_exprt{left, right};
    // ES2024 sec-addition-operator-plus: Numeric addition
    if(
      left.type().id() == ID_floatbv && right.type().id() == ID_floatbv &&
      !is_typescript_string_type(left.type()) &&
      !is_typescript_string_type(right.type()))
    {
      exprt rm = symbol_exprt{"__CPROVER_rounding_mode", signedbv_typet{32}};
      ieee_float_op_exprt result{left, ID_floatbv_plus, right, rm};
      result.type() = left.type();
      return std::move(result);
    }
    // ES2024 sec-addition-operator-plus: String concatenation
    // (includes number/boolean-to-string coercion when one side is a string)
    if(
      is_typescript_string_type(left.type()) ||
      is_typescript_string_type(right.type()))
    {
      // Helper: coerce a non-string operand to its string representation
      // for constant values (matches JS Number.prototype.toString /
      // Boolean.prototype.toString).
      auto coerce_to_string = [this](exprt &operand)
      {
        if(is_typescript_string_type(operand.type()))
          return;
        if(operand.type().id() == ID_floatbv && operand.is_constant())
        {
          ieee_floatt v{to_constant_expr(operand), ieee_floatt::ROUND_TO_EVEN};
          std::string s;
          if(v.is_NaN())
            s = "NaN";
          else if(v.is_infinity())
            s = v.get_sign() ? "-Infinity" : "Infinity";
          else
          {
            // If the value is an integer, format without decimal point.
            // ieee_floatt::round_to_integral() returns a new value —
            // it does not modify the receiver. Assign the result.
            ieee_floatt rounded = v.round_to_integral();
            if(rounded == v)
            {
              mp_integer i = v.to_integer();
              s = integer2string(i);
            }
            else
            {
              s = v.to_ansi_c_string();
            }
          }
          operand = convert_string_literal_from_text(s);
        }
        else if(operand.type().id() == ID_bool && operand.is_constant())
        {
          operand = convert_string_literal_from_text(
            operand.is_true() ? "true" : "false");
        }
        else if(
          operand.type().id() == ID_struct &&
          to_struct_type(operand.type()).get_tag() == "typescript_array")
        {
          // ES2024 §23.1.3.32 Array.prototype.toString → .join(",").
          exprt resolved = operand;
          if(resolved.id() == ID_symbol)
          {
            const symbolt *s =
              symbol_table.lookup(to_symbol_expr(resolved).get_identifier());
            if(s && !s->value.is_nil())
              resolved = s->value;
          }
          if(resolved.id() == ID_struct && resolved.operands().size() >= 2)
          {
            mp_integer len{0};
            if(resolved.operands()[0].is_constant())
              to_integer(to_constant_expr(resolved.operands()[0]), len);
            const exprt &data = resolved.operands()[1];
            std::string joined;
            for(mp_integer i = 0; i < len; ++i)
            {
              auto idx = i.to_ulong();
              if(idx >= data.operands().size())
                break;
              if(i > 0)
                joined += ",";
              const exprt &elem = data.operands()[idx];
              std::string sv = extract_string_value(elem);
              if(!sv.empty())
              {
                joined += sv.substr(2);
              }
              else if(elem.is_constant() && elem.type().id() == ID_floatbv)
              {
                ieee_floatt fv{
                  ieee_float_spect::double_precision(),
                  ieee_floatt::rounding_modet::ROUND_TO_EVEN};
                fv.from_expr(to_constant_expr(elem));
                ieee_floatt rounded = fv.round_to_integral();
                if(rounded == fv)
                  joined += integer2string(fv.to_integer());
                else
                  joined += fv.to_ansi_c_string();
              }
            }
            operand = convert_string_literal_from_text(joined);
          }
        }
      };
      coerce_to_string(left);
      coerce_to_string(right);
      std::string ls_raw = extract_string_value(left);
      std::string rs_raw = extract_string_value(right);
      std::string ls = ls_raw.empty() ? "" : ls_raw.substr(2);
      std::string rs = rs_raw.empty() ? "" : rs_raw.substr(2);
      // Fully concrete: synthesize the literal.
      bool left_concrete =
        !is_typescript_string_type(left.type()) || !ls_raw.empty();
      bool right_concrete =
        !is_typescript_string_type(right.type()) || !rs_raw.empty();
      if(left_concrete && right_concrete)
        return convert_string_literal_from_text(ls + rs);
      // Symbolic concatenation via the refined-string solver.
      // With the solver's array_pool::insert made idempotent, the
      // associate_array_to_pointer calls emitted by
      // ts_string_to_refined are safe to re-run along SSA copies.
      if(
        is_typescript_string_type(left.type()) &&
        is_typescript_string_type(right.type()))
      {
        exprt refined_left = ts_string_to_refined(left);
        exprt refined_right = ts_string_to_refined(right);
        return ts_call_string_returning_function(
          ID_cprover_string_concat_func, {refined_left, refined_right});
      }
      // Mixed-type fallback (e.g. string + number after coercion
      // already handled by the earlier coerce_to_string step).
      struct_typet str_type = typescript_string_type();
      const auto &data_type = to_array_type(str_type.components()[1].type());
      exprt::operandst nondet_chars;
      while(nondet_chars.size() < TYPESCRIPT_MAX_STRING_LENGTH)
        nondet_chars.push_back(
          side_effect_expr_nondett{unsignedbv_typet{16}, source_locationt{}});
      exprt llen = is_typescript_string_type(left.type())
                     ? exprt{member_exprt{left, "length", signedbv_typet{32}}}
                     : from_integer(0, signedbv_typet{32});
      exprt rlen = is_typescript_string_type(right.type())
                     ? exprt{member_exprt{right, "length", signedbv_typet{32}}}
                     : from_integer(0, signedbv_typet{32});
      exprt total_len = plus_exprt{llen, rlen};
      return struct_exprt{
        {total_len, array_exprt{std::move(nondet_chars), data_type}}, str_type};
    }
    return plus_exprt{left, right};
  }
  // ES2024 sec-subtraction-operator-minus
  if(op == "MinusToken")
  {
    if(left.type().id() == ID_floatbv)
    {
      exprt rm = symbol_exprt{"__CPROVER_rounding_mode", signedbv_typet{32}};
      ieee_float_op_exprt result{left, ID_floatbv_minus, right, rm};
      result.type() = left.type();
      return std::move(result);
    }
    return minus_exprt{left, right};
  }
  // ES2024 sec-multiplicative-operators
  if(op == "AsteriskToken")
  {
    if(left.type().id() == ID_floatbv)
    {
      exprt rm = symbol_exprt{"__CPROVER_rounding_mode", signedbv_typet{32}};
      ieee_float_op_exprt result{left, ID_floatbv_mult, right, rm};
      result.type() = left.type();
      return std::move(result);
    }
    return mult_exprt{left, right};
  }
  if(op == "SlashToken")
  {
    if(div_by_zero_check)
    {
      exprt zero = from_integer(0, right.type());
      exprt cond = right.type().id() == ID_floatbv
                     ? exprt{ieee_float_notequal_exprt{right, zero}}
                     : exprt{notequal_exprt{right, zero}};
      code_assertt div_assert{cond};
      div_assert.add_source_location().set_property_class("division-by-zero");
      div_assert.add_source_location().set_comment("division by zero");
      pending_stmts.push_back(std::move(div_assert));
    }
    if(left.type().id() == ID_floatbv)
    {
      if(left.is_constant() && right.is_constant())
      {
        ieee_floatt lv{
          ieee_float_spect::double_precision(),
          ieee_floatt::rounding_modet::ROUND_TO_EVEN};
        ieee_floatt rv{
          ieee_float_spect::double_precision(),
          ieee_floatt::rounding_modet::ROUND_TO_EVEN};
        lv.from_expr(to_constant_expr(left));
        rv.from_expr(to_constant_expr(right));
        lv /= rv;
        return lv.to_expr();
      }
      exprt rm = symbol_exprt{"__CPROVER_rounding_mode", signedbv_typet{32}};
      ieee_float_op_exprt result{left, ID_floatbv_div, right, rm};
      result.type() = left.type();
      if(nan_check)
      {
        code_assertt nan_assert{not_exprt{isnan_exprt{result}}};
        nan_assert.add_source_location().set_property_class("NaN");
        nan_assert.add_source_location().set_comment(
          "NaN check on division result");
        pending_stmts.push_back(std::move(nan_assert));
      }
      return std::move(result);
    }
    return div_exprt{left, right};
  }
  // ES2024 sec-exponentiation-operator: **
  if(op == "AsteriskAsteriskToken")
  {
    // x ** y — for constant y, unroll; otherwise use nondet
    if(right.is_constant() && right.type().id() == ID_floatbv)
    {
      ieee_floatt fv{
        ieee_float_spect::double_precision(),
        ieee_floatt::rounding_modet::ROUND_TO_EVEN};
      fv.from_expr(to_constant_expr(right));
      double exp_val = std::stod(fv.to_ansi_c_string());
      if(left.is_constant())
      {
        ieee_floatt base{
          ieee_float_spect::double_precision(),
          ieee_floatt::rounding_modet::ROUND_TO_EVEN};
        base.from_expr(to_constant_expr(left));
        double result = std::pow(std::stod(base.to_ansi_c_string()), exp_val);
        ieee_floatt res{
          ieee_float_spect::double_precision(),
          ieee_floatt::rounding_modet::ROUND_TO_EVEN};
        res.from_double(result);
        return res.to_expr();
      }
    }
    return side_effect_expr_nondett{double_type(), get_location(node)};
  }
  // ES2024 sec-numeric-types-number-remainder
  if(op == "PercentToken")
  {
    // ES2024 sec-exponentiation-operator: **
    if(op == "AsteriskAsteriskToken")
    {
      // x ** y — for constant y, unroll; otherwise use nondet
      if(right.is_constant() && right.type().id() == ID_floatbv)
      {
        ieee_floatt fv{
          ieee_float_spect::double_precision(),
          ieee_floatt::rounding_modet::ROUND_TO_EVEN};
        fv.from_expr(to_constant_expr(right));
        double exp_val = std::stod(fv.to_ansi_c_string());
        if(left.is_constant())
        {
          ieee_floatt base{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          base.from_expr(to_constant_expr(left));
          double result = std::pow(std::stod(base.to_ansi_c_string()), exp_val);
          ieee_floatt res{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          res.from_double(result);
          return res.to_expr();
        }
      }
      return side_effect_expr_nondett{double_type(), get_location(node)};
    }
    // ES2024 sec-numeric-types-number-remainder:
    // For floats, % is fmod: a - trunc(a/b) * b
    if(left.type().id() == ID_floatbv)
    {
      // a % b = a - trunc(a / b) * b
      exprt rm = symbol_exprt{"__CPROVER_rounding_mode", signedbv_typet{32}};
      ieee_float_op_exprt div{left, ID_floatbv_div, right, rm};
      div.type() = left.type();
      // trunc: convert to integer and back
      exprt trunc = floatbv_typecast_exprt{
        typecast_exprt{div, signedbv_typet{64}}, rm, left.type()};
      ieee_float_op_exprt prod{trunc, ID_floatbv_mult, right, rm};
      prod.type() = left.type();
      ieee_float_op_exprt result{left, ID_floatbv_minus, prod, rm};
      result.type() = left.type();
      return std::move(result);
    }
    return mod_exprt{left, right};
  }

  // ES2024 sec-abstract-equality-comparison: ==
  // Performs type coercion: null == undefined is true,
  // number == string coerces string to number
  if(op == "EqualsEqualsToken")
  {
    // ES2024: null == undefined → true (and vice versa).
    // Both are modeled as NaN; NaN == NaN is false under IEEE 754,
    // so we special-case: two NaNs compare == for loose equality.
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
    // Type coercion: if types differ, cast to common type
    if(left.type() != right.type())
    {
      if(left.type().id() == ID_floatbv && right.type().id() != ID_floatbv)
        right = typecast_exprt{right, left.type()};
      else if(right.type().id() == ID_floatbv && left.type().id() != ID_floatbv)
        left = typecast_exprt{left, right.type()};
      else if(left.type().id() == ID_bool)
        left = typecast_exprt{left, right.type()};
      else if(right.type().id() == ID_bool)
        right = typecast_exprt{right, left.type()};
    }
    if(left.type().id() == ID_floatbv)
    {
      // Symbolic NaN check: `x == undefined` / `x == null` where the
      // constant side is NaN — return isNaN(other).
      if(is_nan_const(right))
        return ieee_float_notequal_exprt{left, left};
      if(is_nan_const(left))
        return ieee_float_notequal_exprt{right, right};
      return ieee_float_equal_exprt{left, right};
    }
    return equal_exprt{left, right};
  }
  // ES2024 sec-abstract-equality-comparison: != (negation of ==)
  if(op == "ExclamationEqualsToken")
  {
    if(left.type() != right.type())
    {
      if(left.type().id() == ID_floatbv && right.type().id() != ID_floatbv)
        right = typecast_exprt{right, left.type()};
      else if(right.type().id() == ID_floatbv && left.type().id() != ID_floatbv)
        left = typecast_exprt{left, right.type()};
      else if(left.type().id() == ID_bool)
        left = typecast_exprt{left, right.type()};
      else if(right.type().id() == ID_bool)
        right = typecast_exprt{right, left.type()};
    }
    if(left.type().id() == ID_floatbv)
      return ieee_float_notequal_exprt{left, right};
    return notequal_exprt{left, right};
  }

  // ES2024 sec-isstrictlyequal: ===
  if(op == "EqualsEqualsEqualsToken")
  {
    // typeof x === "type_string" on union types
    // left is __tag (signedbv[32]) from member_exprt, right is a string literal
    if(
      left.type().id() == ID_signedbv && left.id() == ID_member &&
      is_typescript_string_type(right.type()))
    {
      std::string rs = extract_string_value(right);
      if(!rs.empty())
      {
        std::string type_name = rs.substr(2);
        typet target = convert_type(type_name);
        // Find the tag index by matching the target type in the union
        const exprt &compound = to_member_expr(left).compound();
        if(
          compound.type().id() == ID_struct &&
          to_struct_type(compound.type()).get_tag() == "typescript_union")
        {
          const auto &ust = to_struct_type(compound.type());
          for(std::size_t c = 1; c < ust.components().size(); ++c)
          {
            if(ust.components()[c].type() == target)
              return equal_exprt{left, from_integer(c - 1, signedbv_typet{32})};
          }
        }
      }
    }
    // Handle type mismatch (null comparison or other cross-type ===).
    // Per ES2024 sec-isstrictlyequal: if Type(x) !== Type(y) → false.
    if(left.type() != right.type())
    {
      // Both sides being NaN (null/undefined sentinels) → equal.
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
      {
        // Both are NaN constants. With distinct payloads:
        // - Same nullish sentinel (null===null, undef===undef) → true
        // - Real NaN === Real NaN → false (spec)
        // - null === undefined → false (different payloads)
        if(
          ts_is_nan_payload(left, TS_NAN_PAYLOAD_REAL) ||
          ts_is_nan_payload(right, TS_NAN_PAYLOAD_REAL))
          return false_exprt{};
        // Both are nullish sentinels: true iff same payload.
        return left == right ? exprt{true_exprt{}} : exprt{false_exprt{}};
      }
      // x === null where x is a non-nullable concrete type: false.
      if(is_typescript_string_type(left.type()) && is_nan_const(right))
        return false_exprt{};
      if(is_typescript_string_type(right.type()) && is_nan_const(left))
        return false_exprt{};
      if(left.type().id() == ID_bool && is_nan_const(right))
        return false_exprt{};
      if(right.type().id() == ID_bool && is_nan_const(left))
        return false_exprt{};
      // x === null (old model: right is signedbv 0) → false for typed x.
      if(
        (right.is_constant() && right.type().id() == ID_signedbv) ||
        (left.is_constant() && left.type().id() == ID_signedbv))
        return false_exprt{};
      right = typecast_exprt(right, left.type());
    }
    if(left.type().id() == ID_floatbv)
    {
      // ES2024: `x === undefined` where undefined is our NaN sentinel.
      // Per IEEE-754, NaN === NaN is false, but we want `isNaN(x)` here.
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
      if(is_nan_const(right))
      {
        // x === NaN/null/undefined: compare bit patterns.
        // Real NaN (payload 0): spec says always false.
        if(ts_is_nan_payload(right, TS_NAN_PAYLOAD_REAL))
          return false_exprt{};
        // null or undefined sentinel: true iff left has the same bits.
        return equal_exprt{left, right};
      }
      if(is_nan_const(left))
      {
        if(ts_is_nan_payload(left, TS_NAN_PAYLOAD_REAL))
          return false_exprt{};
        return equal_exprt{left, right};
      }
      // Neither side is a constant NaN. Per ES2024 §7.2.14:
      // - NaN === NaN → false (for real NaN, payload 0)
      // - +0 === -0 → true
      // - null === null → true (same payload)
      // - undefined === undefined → true (same payload)
      // Use ieee_float_equal (handles +0/-0 correctly, returns false
      // for NaN===NaN) OR bit-equal for nullish sentinels.
      // Combined: (ieee_float_equal(l, r) || (l == r && l != realNaN))
      // This gives:
      //   normal numbers: ieee_float_equal handles correctly
      //   +0/-0: ieee_float_equal returns true
      //   NaN===NaN: ieee_float_equal returns false, bit-eq is true
      //     but l==realNaN so the second branch is false → overall false
      //   null===null: ieee_float_equal returns false (it's NaN),
      //     bit-eq is true, l!=realNaN → overall true ✓
      exprt ieee_eq = ieee_float_equal_exprt{left, right};
      exprt bit_eq = equal_exprt{left, right};
      exprt not_real_nan =
        notequal_exprt{left, ts_nan_with_payload(TS_NAN_PAYLOAD_REAL)};
      return or_exprt{ieee_eq, and_exprt{bit_eq, not_real_nan}};
    }
    // Constant string equality
    if(is_typescript_string_type(left.type()))
    {
      std::string ls = extract_string_value(left);
      std::string rs = extract_string_value(right);
      if(!ls.empty() && !rs.empty())
        return ls == rs ? exprt{true_exprt{}} : exprt{false_exprt{}};
      // Provenance-gated solver routing for string ===. When at
      // least one operand is the RESULT of a refined-string solver
      // call (concat, trim, toUpperCase, etc.), the per-slot
      // struct compare over TYPESCRIPT_MAX_STRING_LENGTH positions
      // generates TYPESCRIPT_MAX_STRING_LENGTH
      // cprover_string_char_at_func calls per solver-produced
      // operand, which boolbv records as uninterpreted functions.
      // functionst::add_function_constraints then emits quadratic
      // Ackermann extensionality between every pair of calls,
      // which for e.g. `s.trim() === "hello"` blew up to ~62 M SAT
      // clauses. Routing those cases through
      // cprover_string_equal_func emits a single compact
      // length-plus-forall-i axiom instead.
      //
      // Only gate on solver-produced operands: an
      // assume-side `s === "literal"` where `s` is a plain
      // nondet_string() MUST stay on the per-slot path, because
      // the solver's refinement loop otherwise reports "current
      // index set is empty, this should not happen" for that
      // shape (the assume's universal quantifier has no index
      // set to instantiate from on its own). Literal receivers
      // are identified by `extract_string_value` returning a
      // value; plain nondet receivers fall through to the
      // existing per-slot compare below.
      auto is_solver_produced = [](const exprt &e)
      {
        // Walk the struct's data field looking for any
        // function_application_exprt. ts_call_string_returning_function
        // builds the inline data as an array_exprt of if_exprt
        // guards around cprover_string_char_at_func calls; a
        // plain literal struct has an array_exprt of constant
        // chars with no function_applications.
        if(e.id() != ID_struct)
          return false;
        if(e.operands().size() < 2)
          return false;
        bool found = false;
        e.operands()[1].visit_post(
          [&](const exprt &sub)
          {
            if(sub.id() == ID_function_application)
              found = true;
          });
        return found;
      };
      if(
        is_typescript_string_type(left.type()) &&
        is_typescript_string_type(right.type()) &&
        (is_solver_produced(left) || is_solver_produced(right)))
      {
        exprt refined_left = ts_string_to_refined(left);
        exprt refined_right = ts_string_to_refined(right);
        refined_string_typet refined_ty =
          to_refined_string_type(refined_left.type());
        if(symbol_table.lookup(ID_cprover_string_equal_func) == nullptr)
        {
          std::vector<typet> arg_types = {refined_ty, refined_ty};
          mathematical_function_typet ft(std::move(arg_types), bool_typet{});
          symbolt fs{ID_cprover_string_equal_func, ft, "typescript"};
          fs.base_name = id2string(ID_cprover_string_equal_func);
          symbol_table.add(fs);
        }
        function_application_exprt app(
          symbol_exprt{
            ID_cprover_string_equal_func,
            symbol_table.lookup_ref(ID_cprover_string_equal_func).type},
          {refined_left, refined_right});
        app.type() = bool_typet{};
        return std::move(app);
      }
    }
    // ES2024 sec-isstrictlyequal: when types differ, === is always false
    // (with one exception: our NaN sentinel for null/undefined should
    // compare equal when both sides are null/undefined).
    if(left.type() != right.type())
    {
      // Check: is one side a string struct and the other NaN (null/
      // undefined)? Then false — the spec says "Type(x) !== Type(y)
      // → return false".
      if(
        is_typescript_string_type(left.type()) &&
        right.type().id() == ID_floatbv)
        return false_exprt{};
      if(
        is_typescript_string_type(right.type()) &&
        left.type().id() == ID_floatbv)
        return false_exprt{};
      // For other type mismatches, fall through to equal_exprt
      // (which may produce bottom but matches prior behaviour).
    }
    // For string structs where at least one side has symbolic data
    // (e.g. charAt result containing s.data[i]), a plain
    // equal_exprt on the two struct expressions gets constant-
    // folded to false by the simplifier (it sees non-identical
    // sub-expressions and concludes inequality). Route through
    // per-element comparison: length == length AND data[0] ==
    // data[0] AND ... up to the shorter constant length.
    if(
      is_typescript_string_type(left.type()) &&
      is_typescript_string_type(right.type()))
    {
      // Check if either side is a constant literal (all data slots
      // are constant). If both are constant, equal_exprt is fine
      // (the simplifier handles it). If at least one has symbolic
      // sub-expressions (or is a symbol whose value may be
      // symbolic), use per-element comparison.
      auto is_fully_constant = [](const exprt &e) -> bool
      {
        // A symbol is never fully constant at conversion time —
        // its runtime value depends on the execution path. Only
        // struct literals with all-constant sub-expressions are
        // fully constant (and can be compared via equal_exprt
        // without the simplifier misfolding).
        if(e.id() != ID_struct)
          return false;
        if(e.operands().size() < 2)
          return false;
        bool all_const = true;
        e.visit_post(
          [&](const exprt &sub)
          {
            if(
              sub.id() == ID_symbol || sub.id() == ID_index ||
              sub.id() == ID_member || sub.id() == ID_side_effect ||
              sub.id() == ID_if)
              all_const = false;
          });
        return all_const;
      };
      if(!is_fully_constant(left) || !is_fully_constant(right))
      {
        // Per-field comparison that the solver can handle.
        struct_typet str_type = typescript_string_type();
        typet len_type = str_type.components()[0].type();
        const auto &data_type = to_array_type(str_type.components()[1].type());
        exprt l_len = member_exprt{left, "length", len_type};
        exprt r_len = member_exprt{right, "length", len_type};
        exprt l_data = member_exprt{left, "data", data_type};
        exprt r_data = member_exprt{right, "data", data_type};
        exprt result = equal_exprt{l_len, r_len};
        for(std::size_t i = 0; i < TYPESCRIPT_MAX_STRING_LENGTH; ++i)
        {
          exprt li = index_exprt{l_data, from_integer(i, signedbv_typet{64})};
          exprt ri = index_exprt{r_data, from_integer(i, signedbv_typet{64})};
          result = and_exprt{result, equal_exprt{li, ri}};
        }
        return result;
      }
    }
    return equal_exprt{left, right};
  }

  // ES2024 sec-isstrictlyequal: !==
  if(op == "ExclamationEqualsEqualsToken")
  {
    if(left.type().id() == ID_floatbv && right.type().id() == ID_floatbv)
      return ieee_float_notequal_exprt{left, right};
    // Null comparison: x !== null
    if(left.type() != right.type())
    {
      if(right.type().id() == ID_signedbv && right.is_constant())
      {
        mp_integer rv;
        if(!to_integer(to_constant_expr(right), rv) && rv == 0)
          return true_exprt{}; // x !== null is true when x is not nullable
      }
      right = typecast_exprt(right, left.type());
    }
    return notequal_exprt{left, right};
  }

  // ES2024 sec-relational-operators.
  // For constant strings, compare lexicographically by code unit
  // at conversion time. For non-constant strings, return nondet
  // (TODO: solver-side via cprover_string_compare_func).
  if(
    (op == "LessThanToken" || op == "FirstBinaryOperator" ||
     op == "GreaterThanToken" || op == "LessThanEqualsToken" ||
     op == "GreaterThanEqualsToken") &&
    is_typescript_string_type(left.type()) &&
    is_typescript_string_type(right.type()))
  {
    std::string ls_raw = extract_string_value(left);
    std::string rs_raw = extract_string_value(right);
    if(!ls_raw.empty() && !rs_raw.empty())
    {
      std::string ls = ls_raw.substr(2);
      std::string rs = rs_raw.substr(2);
      bool result = false;
      int cmp = ls.compare(rs);
      if(op == "LessThanToken" || op == "FirstBinaryOperator")
        result = cmp < 0;
      else if(op == "GreaterThanToken")
        result = cmp > 0;
      else if(op == "LessThanEqualsToken")
        result = cmp <= 0;
      else // GreaterThanEqualsToken
        result = cmp >= 0;
      return result ? exprt{true_exprt{}} : exprt{false_exprt{}};
    }
    // Fall through to solver-side for non-constant operands.
    // cprover_string_compare_to_func(s1, s2) returns a signed int:
    //   = 0 if s1 == s2 lexicographically
    //   < 0 if s1 < s2
    //   > 0 if s1 > s2
    // Map our operator to a relation on that result. The function
    // application must be ASSIGNED to a fresh symbol — otherwise
    // the boolbv layer sees the application as an operand of the
    // relational operator and falls through to "ignoring", since
    // string-refinement only intercepts function_application that
    // the solver can resolve to a known result type.
    exprt refined_left = ts_string_to_refined(left);
    exprt refined_right = ts_string_to_refined(right);
    irep_idt cmp_id = ID_cprover_string_compare_to_func;
    if(symbol_table.lookup(cmp_id) == nullptr)
    {
      refined_string_typet refined_ty =
        to_refined_string_type(refined_left.type());
      std::vector<typet> arg_types = {refined_ty, refined_ty};
      mathematical_function_typet ft(std::move(arg_types), signedbv_typet{32});
      symbolt fs{cmp_id, ft, "typescript"};
      fs.base_name = id2string(cmp_id);
      symbol_table.add(fs);
    }
    function_application_exprt::argumentst call_args = {
      refined_left, refined_right};
    function_application_exprt app(
      symbol_exprt{cmp_id, symbol_table.lookup_ref(cmp_id).type},
      std::move(call_args));
    app.type() = signedbv_typet{32};
    // Allocate a fresh int32 symbol to hold the comparison result;
    // emit it as a pending assignment so the boolbv/string-refine
    // layers see a normal symbol read in the relational expression.
    static unsigned cmp_ctr = 0;
    std::string cmp_name = "__ts_strcmp_" + std::to_string(cmp_ctr++);
    irep_idt cmp_sym_id{"typescript::" + cmp_name};
    symbolt cs{cmp_sym_id, signedbv_typet{32}, "typescript"};
    cs.base_name = cmp_name;
    cs.is_lvalue = true;
    cs.is_state_var = true;
    symbol_table.add(cs);
    exprt cmp_sym = symbol_table.lookup_ref(cmp_sym_id).symbol_expr();
    pending_stmts.push_back(code_frontend_assignt{cmp_sym, std::move(app)});
    exprt zero = from_integer(0, signedbv_typet{32});
    if(op == "LessThanToken" || op == "FirstBinaryOperator")
      return binary_relation_exprt{cmp_sym, ID_lt, zero};
    if(op == "GreaterThanToken")
      return binary_relation_exprt{cmp_sym, ID_gt, zero};
    if(op == "LessThanEqualsToken")
      return binary_relation_exprt{cmp_sym, ID_le, zero};
    return binary_relation_exprt{cmp_sym, ID_ge, zero};
  }
  if(op == "LessThanToken" || op == "FirstBinaryOperator")
    return binary_relation_exprt{left, ID_lt, right};
  if(op == "GreaterThanToken")
    return binary_relation_exprt{left, ID_gt, right};
  if(op == "LessThanEqualsToken")
    return binary_relation_exprt{left, ID_le, right};
  if(op == "GreaterThanEqualsToken")
    return binary_relation_exprt{left, ID_ge, right};

  // ES2024 sec-binary-logical-operators (§13.13).
  // && and || are short-circuit operators that return one of the
  // operand values (not a boolean), with the selection based on
  // ToBoolean of the left operand. `1 && 42` returns 42, not `true`.
  if(op == "AmpersandAmpersandToken" || op == "BarBarToken")
  {
    exprt l_bool = left.type().id() == ID_bool ? left : ts_to_boolean(left);
    // If the two operands have the same type, we can emit a simple
    // if-expression selecting between them. That covers the common
    // number && number / string || string cases. Otherwise fall
    // back to a boolean result (the old behaviour) — rare in typed
    // TS code since the result type would be a union.
    if(left.type() == right.type())
    {
      // && returns right when left is truthy, else left.
      // || returns left when left is truthy, else right.
      if(op == "AmpersandAmpersandToken")
        return if_exprt{l_bool, right, left};
      else
        return if_exprt{l_bool, left, right};
    }
    exprt r_bool = right.type().id() == ID_bool ? right : ts_to_boolean(right);
    if(op == "AmpersandAmpersandToken")
      return and_exprt{l_bool, r_bool};
    else
      return or_exprt{l_bool, r_bool};
  }

  // ES2024 sec-assignment-operators: simple assignment
  if(op == "EqualsToken" || op == "FirstAssignment")
  {
    // This is handled at the statement level
    return right;
  }

  // ES2024 sec-comma-operator
  if(op == "CommaToken")
    return right; // evaluate both, return right
  // ES2024 sec-bitwise-operators
  // ES2024 §13.10 bitwise operators: all arg values are first
  // converted to int32 (ToInt32), except for `>>>` which also
  // converts the result back as uint32. Shift counts are masked to
  // the low 5 bits (mod 32).
  // For BigInt operands (§6.1.6.2.9): bitwise ops work on the full
  // width without int32 truncation. `>>>` is not allowed on bigint
  // per spec (TypeError); we treat it as signed shift.
  if(
    op == "AmpersandToken" || op == "BarToken" || op == "CaretToken" ||
    op == "LessThanLessThanToken" || op == "GreaterThanGreaterThanToken" ||
    op == "GreaterThanGreaterThanGreaterThanToken")
  {
    if(
      is_typescript_bigint_type(left.type()) &&
      is_typescript_bigint_type(right.type()))
    {
      if(op == "AmpersandToken")
        return bitand_exprt{left, right};
      if(op == "BarToken")
        return bitor_exprt{left, right};
      if(op == "CaretToken")
        return bitxor_exprt{left, right};
      if(op == "LessThanLessThanToken")
        return shl_exprt{left, right};
      // >> and >>> both do arithmetic shift for bigint
      return ashr_exprt{left, right};
    }
    // Convert floats to 32-bit integers, apply op, convert back
    exprt l_int = typecast_exprt{left, signedbv_typet{32}};
    exprt r_int = typecast_exprt{right, signedbv_typet{32}};
    exprt result_int;
    if(op == "AmpersandToken")
      result_int = bitand_exprt{l_int, r_int};
    else if(op == "BarToken")
      result_int = bitor_exprt{l_int, r_int};
    else if(op == "CaretToken")
      result_int = bitxor_exprt{l_int, r_int};
    else
    {
      // Shift: mask count to low 5 bits (§13.10.1 step 5).
      exprt masked_r =
        bitand_exprt{r_int, from_integer(31, signedbv_typet{32})};
      if(op == "LessThanLessThanToken")
        result_int = shl_exprt{l_int, masked_r};
      else if(op == "GreaterThanGreaterThanToken")
        result_int = ashr_exprt{l_int, masked_r};
      else
      {
        // >>>: zero-extending right shift. Reinterpret LHS as
        // uint32, shift, keep result as uint32, then widen to int64
        // before casting to double so the top bit isn't misread as
        // a sign bit.
        exprt l_uint = typecast_exprt{l_int, unsignedbv_typet{32}};
        exprt masked_r_u = typecast_exprt{masked_r, unsignedbv_typet{32}};
        exprt shifted = lshr_exprt{l_uint, masked_r_u};
        exprt widened = typecast_exprt{shifted, signedbv_typet{64}};
        return typecast_exprt{widened, double_type()};
      }
    }
    return typecast_exprt{result_int, double_type()};
  }
  // ES2024 sec-nullish-coalescing: ??
  if(op == "QuestionQuestionToken")
  {
    // x ?? y → (x is null or undefined) ? y : x
    // With distinct NaN payloads: null is payload 1, undefined is
    // payload 2. Check if x has either of those bit patterns.
    // Real NaN (payload 0) is NOT nullish per spec — `NaN ?? y`
    // returns NaN.
    if(left.type().id() == ID_floatbv)
    {
      exprt is_null =
        equal_exprt{left, ts_nan_with_payload(TS_NAN_PAYLOAD_NULL)};
      exprt is_undef =
        equal_exprt{left, ts_nan_with_payload(TS_NAN_PAYLOAD_UNDEFINED)};
      exprt is_nullish = or_exprt{is_null, is_undef};
      exprt right_conv = right;
      if(right.type() != left.type())
        right_conv = typecast_exprt{right, left.type()};
      return if_exprt{is_nullish, right_conv, left};
    }
    return left;
  }
  // ES2024 sec-assignment-operators: logical assignment
  if(op == "AmpersandAmpersandEqualsToken")
  {
    // x &&= y → if(x) x = y
    // For numbers: truthy means non-zero
    return if_exprt{ts_to_boolean(left), right, left};
  }
  if(op == "BarBarEqualsToken")
  {
    // x ||= y → if(!x) x = y
    return if_exprt{ts_to_boolean(left), left, right};
  }
  // ES2024 sec-assignment-operators: compound assignment
  if(op == "FirstCompoundAssignment" || op == "PlusEqualsToken")
    return plus_exprt{left, right};
  if(op == "MinusEqualsToken")
    return minus_exprt{left, right};
  if(op == "AsteriskEqualsToken")
    return mult_exprt{left, right};
  if(op == "SlashEqualsToken")
    return div_exprt{left, right};
  if(op == "PercentEqualsToken")
    return mod_exprt{left, right};
  // ES2024 sec-instanceofoperator
  // TSH: Narrowing > instanceof narrowing
  // Note: in static analysis, always true for matching types
  if(op == "InstanceOfKeyword")
    return true_exprt{};

  // ES2024 sec-relational-operators: 'in' operator
  // TSH: Narrowing > in operator narrowing
  // "prop" in obj — true if obj's type has a property named prop
  if(op == "InKeyword")
  {
    // ES2024 §13.10.2 — property / index membership.
    // For an array receiver with a numeric left operand, the check
    // is `0 <= i < arr.length` (our model has no holes). Handle
    // this before the string-key path.
    if(
      right.type().id() == ID_struct &&
      to_struct_type(right.type()).get_tag() == "typescript_array" &&
      left.type().id() == ID_floatbv)
    {
      // Resolve the symbolic array if possible.
      exprt arr = right;
      if(arr.id() == ID_symbol)
      {
        const symbolt *s =
          symbol_table.lookup(to_symbol_expr(arr).get_identifier());
        if(s && !s->value.is_nil())
          arr = s->value;
      }
      if(arr.id() == ID_struct && arr.operands().size() >= 2)
      {
        // Extract the index value, handling unary-minus of a
        // constant (like `-1 in a`).
        mp_integer idx;
        bool got_idx = false;
        const exprt *le = &left;
        bool neg = false;
        if(le->id() == ID_unary_minus && le->operands().size() == 1)
        {
          neg = true;
          le = &le->operands()[0];
        }
        if(le->is_constant() && le->type().id() == ID_floatbv)
        {
          ieee_floatt fv{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          fv.from_expr(to_constant_expr(*le));
          idx = fv.to_integer();
          if(neg)
            idx = -idx;
          got_idx = true;
        }
        if(got_idx)
        {
          mp_integer len{0};
          if(arr.operands()[0].is_constant())
            to_integer(to_constant_expr(arr.operands()[0]), len);
          if(idx >= 0 && idx < len)
            return exprt{true_exprt{}};
          return exprt{false_exprt{}};
        }
      }
    }
    std::string prop_name;
    std::string ls = extract_string_value(left);
    if(!ls.empty())
      prop_name = ls.substr(2);
    if(!prop_name.empty() && right.type().id() == ID_struct)
    {
      const auto &st = to_struct_type(right.type());
      return st.has_component(prop_name) ? exprt{true_exprt{}}
                                         : exprt{false_exprt{}};
    }
    return side_effect_expr_nondett{bool_typet{}, source_locationt{}};
  }

  log.warning() << "Unsupported binary operator: " << op
                << " (please file an issue with a minimal reproduction); "
                << "returning nondet" << messaget::eom;
  return nil_exprt{};
}

// ES2024 sec-unary-operators
exprt typescript_convertert::convert_prefix_unary_expression(const jsont &node)
{
  exprt operand = convert_expression(json_member(node, "operand"));
  std::string op = json_string(json_member(node, "operator"));

  if(operand.is_nil())
    return nil_exprt{};

  if(op == "MinusToken")
    return unary_minus_exprt{operand};
  if(op == "PlusToken")
  {
    // ES2024 §13.5.4 UnaryPlus → ToNumber.
    // If the operand is a constant string, parse its digits at
    // conversion time. Empty string → 0. Whitespace-only string → 0.
    // Non-numeric string → NaN.
    if(is_typescript_string_type(operand.type()))
    {
      std::string s = extract_string_value(operand);
      if(!s.empty() && s.substr(0, 2) == "S:")
      {
        std::string raw = s.substr(2);
        // Trim ASCII whitespace per ES2024 §7.1.4.1.2 StringToNumber
        std::size_t start = 0;
        while(start < raw.size() && (raw[start] == ' ' || raw[start] == '\t' ||
                                     raw[start] == '\n' || raw[start] == '\r'))
          start++;
        std::size_t end = raw.size();
        while(end > start && (raw[end - 1] == ' ' || raw[end - 1] == '\t' ||
                              raw[end - 1] == '\n' || raw[end - 1] == '\r'))
          end--;
        std::string trimmed = raw.substr(start, end - start);
        if(trimmed.empty())
          return ieee_floatt::zero(ieee_float_spect::double_precision())
            .to_expr();
        try
        {
          std::size_t pos = 0;
          double d = std::stod(trimmed, &pos);
          if(pos == trimmed.size())
          {
            ieee_floatt v{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            v.from_double(d);
            return v.to_expr();
          }
        }
        catch(...)
        {
        }
        return ts_nan_with_payload(TS_NAN_PAYLOAD_REAL);
      }
      // Symbolic string: route through the refined-string solver via
      // cprover_string_parse_int_func. We build a refined_string_exprt
      // boundary view of our {length, inline-array} struct, emit the
      // solver-side associations (array <-> pointer, length <-> array)
      // as pending statements, then call the parse function.
      //
      // Key subtlety: the solver walks arguments recursively looking
      // for any subexpression of refined_string_type, which it then
      // casts to struct_exprt. Our `operand` is a symbol_exprt whose
      // TYPE has the refined_string tag — so passing a struct whose
      // children (length, pointer) contain member_exprt(operand, ...)
      // would make the walker visit `operand` (symbol, not struct)
      // and invariant-fail. Avoid this by computing length and
      // pointer into scalar-typed temporary symbols FIRST, then
      // building the refined struct from those temporaries.
      typet our_len_type = signedbv_typet{32};
      typet solver_len_type = signedbv_typet{64};
      typet char_type = unsignedbv_typet{16};
      array_typet data_array_type{
        char_type, from_integer(TYPESCRIPT_MAX_STRING_LENGTH, solver_len_type)};
      pointer_typet char_ptr_type{char_type, 64};
      static unsigned str_tmp_ctr = 0;
      auto fresh_symbol = [this](const std::string &base, const typet &t)
      {
        std::string name = base + "_" + std::to_string(str_tmp_ctr++);
        irep_idt id{"typescript::" + name};
        symbolt s{id, t, "typescript"};
        s.base_name = name;
        s.is_lvalue = true;
        s.is_state_var = true;
        symbol_table.add(s);
        return symbol_table.lookup_ref(id).symbol_expr();
      };
      // temp_data: copy of our struct's data array (must be an
      // array-typed symbol so the solver can insert it into
      // array_pool).
      exprt temp_data = fresh_symbol("__ts_str_data", data_array_type);
      pending_stmts.push_back(code_frontend_assignt{
        temp_data, member_exprt{operand, "data", data_array_type}});
      // temp_len: 64-bit length.
      exprt temp_len = fresh_symbol("__ts_str_len", solver_len_type);
      pending_stmts.push_back(code_frontend_assignt{
        temp_len,
        typecast_exprt{
          member_exprt{operand, "length", our_len_type}, solver_len_type}});
      // pointer = &temp_data[0].
      exprt pointer = address_of_exprt{
        index_exprt{temp_data, from_integer(0, solver_len_type), char_type},
        char_ptr_type};
      // Declare the associate_* functions in the symbol table.
      auto declare_assoc_func =
        [this](const irep_idt &name, const typet &arg1_t, const typet &arg2_t)
      {
        if(symbol_table.lookup(name) == nullptr)
        {
          std::vector<typet> arg_types = {arg1_t, arg2_t};
          mathematical_function_typet ft(
            std::move(arg_types), signedbv_typet{32});
          symbolt fs{name, ft, "typescript"};
          fs.base_name = id2string(name);
          symbol_table.add(fs);
        }
      };
      declare_assoc_func(
        ID_cprover_associate_array_to_pointer_func,
        data_array_type,
        char_ptr_type);
      declare_assoc_func(
        ID_cprover_associate_length_to_array_func,
        data_array_type,
        solver_len_type);
      auto emit_assoc =
        [&](const irep_idt &func, const exprt &a, const exprt &b)
      {
        exprt rc = fresh_symbol("__ts_str_assoc_rc", signedbv_typet{32});
        function_application_exprt app(
          symbol_exprt{func, symbol_table.lookup_ref(func).type}, {a, b});
        app.type() = signedbv_typet{32};
        pending_stmts.push_back(code_frontend_assignt{rc, app});
      };
      emit_assoc(
        ID_cprover_associate_array_to_pointer_func, temp_data, pointer);
      emit_assoc(
        ID_cprover_associate_length_to_array_func, temp_data, temp_len);
      // Build the refined_string from the scalar temps.
      refined_string_typet refined_ty{solver_len_type, char_ptr_type};
      exprt refined = struct_exprt{{temp_len, pointer}, refined_ty};
      typet int_result_type = signedbv_typet{32};
      if(symbol_table.lookup(ID_cprover_string_parse_int_func) == nullptr)
      {
        std::vector<typet> arg_types = {refined_ty};
        mathematical_function_typet ft(std::move(arg_types), int_result_type);
        symbolt fs{ID_cprover_string_parse_int_func, ft, "typescript"};
        fs.base_name = id2string(ID_cprover_string_parse_int_func);
        symbol_table.add(fs);
      }
      function_application_exprt app(
        symbol_exprt{
          ID_cprover_string_parse_int_func,
          symbol_table.lookup_ref(ID_cprover_string_parse_int_func).type},
        {refined});
      app.type() = int_result_type;
      return typecast_exprt{app, double_type()};
    }
    return operand; // unary + is identity for numbers
  }
  // ES2024 sec-logical-not-operator
  if(op == "ExclamationToken")
  {
    if(operand.type().id() != ID_bool)
      operand = ts_to_boolean(operand);
    return not_exprt{operand};
  }
  // ES2024 sec-prefix-increment-operator
  if(op == "PlusPlusToken")
    return plus_exprt{operand, from_integer(1, operand.type())};
  if(op == "MinusMinusToken")
    return minus_exprt{operand, from_integer(1, operand.type())};

  log.warning() << "Unsupported unary operator: " << op << messaget::eom;
  return nil_exprt{};
}

// ES2024 sec-function-calls

// --- Integer type inference ---

typet typescript_convertert::number_type_for(const std::string &var_name) const
{
  if(!integer_inference)
    return double_type();
  auto it = inferred_num_kind.find(var_name);
  if(it == inferred_num_kind.end())
    return double_type();
  switch(it->second)
  {
  case num_kindt::INDEX:
    return signedbv_typet{64};
  case num_kindt::INTEGER:
    return signedbv_typet{64};
  case num_kindt::FLOAT:
  default:
    return double_type();
  }
}

void typescript_convertert::infer_integer_types(const jsont &statements)
{
  if(!integer_inference || !statements.is_array())
    return;

  // Collect all variables that are used in integer-only contexts.
  // A variable is INTEGER if:
  //   - Used with %, &, |, ^, <<, >> operators
  //   - Compared with integer literals only
  //   - Assigned only integer literals or other integer variables
  //   - Never used with / that could produce fractions
  // A variable is INDEX if:
  //   - Initialized from .length
  //   - Used as a loop counter (for i = 0; i < N; i++)
  //   - Used as an array index

  // Phase 1: scan for integer indicators
  std::set<std::string> integer_vars; // definitely integer
  std::set<std::string> float_vars;   // definitely float (uses division)
  std::set<std::string> index_vars;   // index/counter

  std::function<void(const jsont &, const std::string &)> scan =
    [&](const jsont &node, const std::string &scope)
  {
    if(!node.is_object())
      return;
    std::string kind = json_string(json_member(node, "_kind"));

    // For loops: the initializer variable is an index
    if(kind == "ForStatement")
    {
      const jsont &init = json_member(node, "initializer");
      if(init.is_object())
      {
        std::string ik = json_string(json_member(init, "_kind"));
        if(ik == "VariableDeclarationList")
        {
          const jsont &decls = json_member(init, "declarations");
          if(decls.is_array())
          {
            for(const auto &d : to_json_array(decls))
            {
              std::string vn =
                json_string(json_member(json_member(d, "name"), "text"));
              if(!vn.empty())
                index_vars.insert(scope + vn);
            }
          }
        }
      }
    }

    // Variable declarations with .length initializer
    if(kind == "VariableDeclaration")
    {
      std::string vn =
        json_string(json_member(json_member(node, "name"), "text"));
      const jsont &init = json_member(node, "initializer");
      if(init.is_object())
      {
        std::string ik = json_string(json_member(init, "_kind"));
        if(ik == "PropertyAccessExpression")
        {
          std::string prop =
            json_string(json_member(json_member(init, "name"), "text"));
          if(prop == "length")
            index_vars.insert(scope + vn);
        }
      }
    }

    // Binary expressions with integer operators
    if(kind == "BinaryExpression")
    {
      std::string op = json_string(json_member(node, "operator"));
      if(
        op == "PercentToken" || op == "AmpersandToken" || op == "BarToken" ||
        op == "CaretToken" || op == "LessThanLessThanToken" ||
        op == "GreaterThanGreaterThanToken" ||
        op == "GreaterThanGreaterThanGreaterThanToken")
      {
        // Both operands are integer
        const jsont &left = json_member(node, "left");
        const jsont &right = json_member(node, "right");
        std::string ln = json_string(json_member(left, "text"));
        std::string rn = json_string(json_member(right, "text"));
        if(!ln.empty())
          integer_vars.insert(scope + ln);
        if(!rn.empty())
          integer_vars.insert(scope + rn);
      }
      // Division with non-integer result → float
      if(op == "SlashToken")
      {
        const jsont &left = json_member(node, "left");
        std::string ln = json_string(json_member(left, "text"));
        if(!ln.empty())
          float_vars.insert(scope + ln);
      }
      // Multiplication can overflow signedbv → mark as float
      if(op == "AsteriskToken")
      {
        const jsont &left = json_member(node, "left");
        const jsont &right = json_member(node, "right");
        std::string ln = json_string(json_member(left, "text"));
        std::string rn = json_string(json_member(right, "text"));
        if(!ln.empty())
          float_vars.insert(scope + ln);
        if(!rn.empty())
          float_vars.insert(scope + rn);
      }
    }

    // Recurse
    for(const auto &kv : to_json_object(node))
    {
      if(kv.second.is_object())
        scan(kv.second, scope);
      else if(kv.second.is_array())
        for(const auto &elem : to_json_array(kv.second))
          scan(elem, scope);
    }
  };

  // Scan all statements
  for(const auto &stmt : to_json_array(statements))
  {
    std::string kind = json_string(json_member(stmt, "_kind"));
    if(kind == "FunctionDeclaration")
    {
      std::string fname =
        json_string(json_member(json_member(stmt, "name"), "text"));
      std::string fscope = fname + "::";
      // Scan function parameters — if used with integer ops, mark them
      scan(stmt, fscope);
      // If any parameter is integer, mark return type as integer too
      const jsont &params = json_member(stmt, "parameters");
      bool any_int_param = false;
      if(params.is_array())
      {
        for(const auto &p : to_json_array(params))
        {
          std::string pn =
            json_string(json_member(json_member(p, "name"), "text"));
          if(integer_vars.count(fscope + pn) || index_vars.count(fscope + pn))
            any_int_param = true;
        }
      }
      if(any_int_param)
        integer_vars.insert(fname + "::__return");
    }
    else
    {
      scan(stmt, "");
    }
  }

  // Conservative propagation: only mark variables that are directly
  // assigned from other integer variables within the same function.
  // Also mark call-site arguments to functions with % operations.
  std::set<std::string> integer_functions;
  for(const auto &v : integer_vars)
  {
    auto sep = v.find("::");
    if(sep != std::string::npos)
      integer_functions.insert(v.substr(0, sep));
  }
  // Mark arguments at call sites to integer functions
  std::function<void(const jsont &)> mark_args = [&](const jsont &n)
  {
    if(!n.is_object())
      return;
    std::string nk = json_string(json_member(n, "_kind"));
    if(nk == "CallExpression")
    {
      const jsont &ce = json_member(n, "expression");
      std::string fn = json_string(json_member(ce, "text"));
      if(integer_functions.count(fn))
      {
        const jsont &ca = json_member(n, "arguments");
        if(ca.is_array())
          for(const auto &a : to_json_array(ca))
          {
            std::string an = json_string(json_member(a, "text"));
            if(!an.empty())
              integer_vars.insert(an);
          }
      }
    }
    for(const auto &kv : to_json_object(n))
    {
      if(kv.second.is_object())
        mark_args(kv.second);
      else if(kv.second.is_array())
        for(const auto &e : to_json_array(kv.second))
          mark_args(e);
    }
  };
  for(const auto &stmt : to_json_array(statements))
    mark_args(stmt);

  // Build final map: index > integer > float
  for(const auto &v : index_vars)
  {
    if(!float_vars.count(v))
      inferred_num_kind[v] = num_kindt::INDEX;
  }
  for(const auto &v : integer_vars)
  {
    if(!float_vars.count(v) && !inferred_num_kind.count(v))
      inferred_num_kind[v] = num_kindt::INTEGER;
  }
}
