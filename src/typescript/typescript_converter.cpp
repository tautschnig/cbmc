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
  // Resolve generic type parameters
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
  // Array types: number[], string[], etc.
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
    return list_type;
  }
  // Default: treat as number for now
  return double_type();
}

// --- Expression conversion ---

exprt typescript_convertert::convert_expression(const jsont &node)
{
  std::string kind = json_string(json_member(node, "_kind"));
  if(kind == "FirstLiteralToken" || kind == "NumericLiteral")
    return convert_numeric_literal(node);
  if(kind == "StringLiteral" || kind == "NoSubstitutionTemplateLiteral")
    return convert_string_literal(node);
  if(kind == "TrueKeyword")
    return true_exprt{};
  if(kind == "FalseKeyword")
    return false_exprt{};
  if(kind == "NullKeyword")
  {
    // Check context type from _type annotation
    std::string ts_type = json_string(json_member(node, "_type"));
    if(ts_type == "null")
    {
      // Use a sentinel value that works with any comparison
      return from_integer(0, signedbv_typet{64});
    }
    return from_integer(0, signedbv_typet{64});
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
    }
    return nil_exprt{};
  }
  // ES2024 sec-element-access: arr[i]
  // ES2024 sec-property-accessors (bracket notation)
  if(kind == "ElementAccessExpression")
  {
    exprt obj = convert_expression(json_member(node, "expression"));
    exprt idx = convert_expression(json_member(node, "argumentExpression"));
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
      cond = typecast_exprt{cond, bool_typet{}};
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
  // TSH: Everyday Types > Template Literal Types
  if(kind == "TemplateExpression")
  {
    // Concatenate head + spans at conversion time
    std::string result;
    bool all_const = true;
    std::string head_text =
      json_string(json_member(json_member(node, "head"), "text"));
    result += head_text;
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
        else if(expr.id() == ID_symbol && expr.type().id() == ID_floatbv)
        {
          // Try to resolve symbol to constant value
          const symbolt *ns =
            symbol_table.lookup(to_symbol_expr(expr).get_identifier());
          if(ns && !ns->value.is_nil() && ns->value.is_constant())
          {
            ieee_floatt fv{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            fv.from_expr(to_constant_expr(ns->value));
            double dval = std::stod(fv.to_ansi_c_string());
            if(dval == std::floor(dval) && std::abs(dval) < 1e15)
              result += std::to_string(static_cast<long long>(dval));
            else
              result += fv.to_ansi_c_string();
          }
          else
            all_const = false;
        }
        else if(expr.is_constant() && expr.type().id() == ID_floatbv)
        {
          // Convert constant number to string
          ieee_floatt fv{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          fv.from_expr(to_constant_expr(expr));
          double dval = std::stod(fv.to_ansi_c_string());
          if(dval == std::floor(dval) && std::abs(dval) < 1e15)
            result += std::to_string(static_cast<long long>(dval));
          else
            result += fv.to_ansi_c_string();
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
    std::string typeof_result = "object";
    if(ts_type == "number")
      typeof_result = "number";
    else if(ts_type == "string")
      typeof_result = "string";
    else if(ts_type == "boolean")
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
  log.warning() << "Unsupported expression: " << kind << messaget::eom;
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
        for(mp_integer i = 0; i < len; ++i)
        {
          auto idx = i.to_ulong();
          if(idx < data.operands().size() && data.operands()[idx].is_constant())
          {
            mp_integer ch;
            if(!to_integer(to_constant_expr(data.operands()[idx]), ch))
              s += static_cast<char>(ch.to_ulong());
          }
        }
        return "S:" + s;
      }
    }
  }
  return "";
}

exprt typescript_convertert::convert_string_literal_from_text(
  const std::string &text)
{
  // Build string as struct{length: signedbv[32], data: unsignedbv[16][MAX]}
  struct_typet str_type = typescript_string_type();
  const auto &data_type = to_array_type(str_type.components()[1].type());

  exprt::operandst chars;
  for(char c : text)
    chars.push_back(
      from_integer(static_cast<unsigned char>(c), unsignedbv_typet{16}));
  while(chars.size() < TYPESCRIPT_MAX_STRING_LENGTH)
    chars.push_back(from_integer(0, unsignedbv_typet{16}));

  exprt length =
    from_integer(static_cast<int>(text.size()), signedbv_typet{32});

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
  if(name == "undefined")
    return from_integer(0, signedbv_typet{64});
  if(name == "NaN")
  {
    ieee_floatt nan{
      ieee_float_spect::double_precision(),
      ieee_floatt::rounding_modet::ROUND_TO_EVEN};
    nan.make_NaN();
    return nan.to_expr();
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

  log.warning() << "Unknown identifier: " << name << messaget::eom;
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

  // Type promotion: ensure both sides have the same type
  // (skip for === and !== which handle type mismatches themselves)
  if(
    left.type() != right.type() && op != "EqualsEqualsEqualsToken" &&
    op != "ExclamationEqualsEqualsToken")
  {
    if(left.type().id() == ID_floatbv)
      right = typecast_exprt{right, left.type()};
    else if(right.type().id() == ID_floatbv)
      left = typecast_exprt{left, right.type()};
  }

  // ES2024 sec-addition-operator-plus
  if(op == "PlusToken")
  {
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
    if(
      is_typescript_string_type(left.type()) ||
      is_typescript_string_type(right.type()))
    {
      std::string ls_raw = extract_string_value(left);
      std::string rs_raw = extract_string_value(right);
      std::string ls = ls_raw.empty() ? "" : ls_raw.substr(2);
      std::string rs = rs_raw.empty() ? "" : rs_raw.substr(2);
      if(!ls.empty() || !rs.empty())
        return convert_string_literal_from_text(ls + rs);
      return side_effect_expr_nondett{
        typescript_string_type(), source_locationt{}};
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
    // null == undefined → true (and vice versa)
    bool left_null = left.is_zero() && left.type().id() == ID_signedbv;
    bool right_null = right.is_zero() && right.type().id() == ID_signedbv;
    if(left_null && right_null)
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
      return ieee_float_equal_exprt{left, right};
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
    // Handle type mismatch (null comparison)
    if(left.type() != right.type())
    {
      // x === null where x is not nullable → false
      if(
        (right.is_constant() && right.type().id() == ID_signedbv) ||
        (left.is_constant() && left.type().id() == ID_signedbv))
        return false_exprt{};
      right = typecast_exprt(right, left.type());
    }
    if(left.type().id() == ID_floatbv)
      return ieee_float_equal_exprt{left, right};
    // Constant string equality
    if(is_typescript_string_type(left.type()))
    {
      std::string ls = extract_string_value(left);
      std::string rs = extract_string_value(right);
      if(!ls.empty() && !rs.empty())
        return ls == rs ? exprt{true_exprt{}} : exprt{false_exprt{}};
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

  // ES2024 sec-relational-operators
  if(op == "LessThanToken" || op == "FirstBinaryOperator")
    return binary_relation_exprt{left, ID_lt, right};
  if(op == "GreaterThanToken")
    return binary_relation_exprt{left, ID_gt, right};
  if(op == "LessThanEqualsToken")
    return binary_relation_exprt{left, ID_le, right};
  if(op == "GreaterThanEqualsToken")
    return binary_relation_exprt{left, ID_ge, right};

  // ES2024 sec-binary-logical-operators
  if(op == "AmpersandAmpersandToken")
  {
    exprt l =
      left.type().id() == ID_bool ? left : typecast_exprt{left, bool_typet{}};
    exprt r = right.type().id() == ID_bool
                ? right
                : typecast_exprt{right, bool_typet{}};
    return and_exprt{l, r};
  }
  if(op == "BarBarToken")
  {
    exprt l =
      left.type().id() == ID_bool ? left : typecast_exprt{left, bool_typet{}};
    exprt r = right.type().id() == ID_bool
                ? right
                : typecast_exprt{right, bool_typet{}};
    return or_exprt{l, r};
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
  if(
    op == "AmpersandToken" || op == "BarToken" || op == "CaretToken" ||
    op == "LessThanLessThanToken" || op == "GreaterThanGreaterThanToken" ||
    op == "GreaterThanGreaterThanGreaterThanToken")
  {
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
    else if(op == "LessThanLessThanToken")
      result_int = shl_exprt{l_int, r_int};
    else if(op == "GreaterThanGreaterThanToken")
      result_int = ashr_exprt{l_int, r_int};
    else
      result_int = lshr_exprt{l_int, r_int};
    return typecast_exprt{result_int, double_type()};
  }
  // ES2024 sec-nullish-coalescing: ??
  if(op == "QuestionQuestionToken")
  {
    // x ?? y → x !== null && x !== undefined ? x : y
    // For numbers: x is never null, so just return x
    return left;
  }
  // ES2024 sec-assignment-operators: logical assignment
  if(op == "AmpersandAmpersandEqualsToken")
  {
    // x &&= y → if(x) x = y
    // For numbers: truthy means non-zero
    return if_exprt{typecast_exprt{left, bool_typet{}}, right, left};
  }
  if(op == "BarBarEqualsToken")
  {
    // x ||= y → if(!x) x = y
    return if_exprt{typecast_exprt{left, bool_typet{}}, left, right};
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

  log.warning() << "Unsupported binary operator: " << op << messaget::eom;
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
    return operand; // unary + is identity for numbers
  // ES2024 sec-logical-not-operator
  if(op == "ExclamationToken")
  {
    if(operand.type().id() != ID_bool)
      operand = typecast_exprt{operand, bool_typet{}};
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
