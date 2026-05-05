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
#include <util/string_expr.h>
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
  // ES2024 sec-ecmascript-language-types-undefined-type
  if(ts_type == "void" || ts_type == "undefined")
    return empty_typet{};
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
  // Object literal types: { x: number; y: number; }
  if(ts_type.size() > 2 && ts_type[0] == '{' && ts_type.back() == '}')
  {
    // Will cache result at end
    // Parse the type string to extract property names and types
    struct_typet st;
    std::string inner =
      ts_type.substr(2, ts_type.size() - 4); // remove "{ " and " }"
    // Split by "; "
    std::size_t pos = 0;
    while(pos < inner.size())
    {
      auto semi = inner.find(';', pos);
      if(semi == std::string::npos)
        semi = inner.size();
      std::string field = inner.substr(pos, semi - pos);
      // Trim
      while(!field.empty() && field[0] == ' ')
        field.erase(0, 1);
      while(!field.empty() && field.back() == ' ')
        field.pop_back();
      auto colon = field.find(':');
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
  // Union types: number | string → tagged union struct
  if(ts_type.find(" | ") != std::string::npos)
  {
    // Parse union members
    std::vector<std::string> members;
    std::string tmp = ts_type;
    while(true)
    {
      auto pos = tmp.find(" | ");
      if(pos == std::string::npos)
      {
        while(!tmp.empty() && tmp[0] == ' ')
          tmp.erase(0, 1);
        while(!tmp.empty() && tmp.back() == ' ')
          tmp.pop_back();
        if(!tmp.empty())
          members.push_back(tmp);
        break;
      }
      std::string m = tmp.substr(0, pos);
      while(!m.empty() && m[0] == ' ')
        m.erase(0, 1);
      while(!m.empty() && m.back() == ' ')
        m.pop_back();
      if(!m.empty())
        members.push_back(m);
      tmp = tmp.substr(pos + 3);
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
    return nil_exprt{}; // handled in convert_variable_statement
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
      // refined_string_typet has length as first component
      const auto &rst = to_refined_string_type(obj.type());
      return member_exprt{obj, "length", rst.get_index_type()};
    }
    if(obj.type().id() == ID_struct)
    {
      const auto &st = to_struct_type(obj.type());
      if(st.has_component(prop))
        return member_exprt{obj, prop, st.get_component(prop).type()};
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
  if(kind == "NewExpression")
  {
    std::string cls_name =
      json_string(json_member(json_member(node, "expression"), "text"));
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
            components.push_back(st.components()[i]);
            fields.push_back(src.operands()[i]);
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
    typet elem_type = double_type(); // default
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
    struct_typet list_type;
    list_type.components().push_back(
      struct_typet::componentt{"length", signedbv_typet{64}});
    list_type.components().push_back(
      struct_typet::componentt{"data", arr_type});
    list_type.set_tag("typescript_array");
    return struct_exprt{
      {from_integer(actual_len, signedbv_typet{64}),
       array_exprt{std::move(elements), arr_type}},
      list_type};
  }
  // ES2024 sec-typeof-operator
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
  // Check refined_string_exprt: {length, address_of(arr[0])}
  if(
    e.id() == ID_struct && e.operands().size() >= 2 &&
    e.operands()[0].is_constant())
  {
    mp_integer len;
    if(!to_integer(to_constant_expr(e.operands()[0]), len))
    {
      const exprt &ptr = e.operands()[1];
      if(
        ptr.id() == ID_address_of && ptr.operands()[0].id() == ID_index &&
        ptr.operands()[0].operands()[0].id() == ID_symbol)
      {
        irep_idt aid =
          to_symbol_expr(ptr.operands()[0].operands()[0]).get_identifier();
        const symbolt *as = symbol_table.lookup(aid);
        if(as && !as->value.is_nil())
        {
          std::string s;
          for(mp_integer i = 0; i < len; ++i)
          {
            auto idx = i.to_ulong();
            if(
              idx < as->value.operands().size() &&
              as->value.operands()[idx].is_constant())
            {
              mp_integer ch;
              if(!to_integer(to_constant_expr(as->value.operands()[idx]), ch))
                s += static_cast<char>(ch.to_ulong());
            }
          }
          return "S:" + s;
        }
      }
    }
  }
  return "";
}

exprt typescript_convertert::convert_string_literal_from_text(
  const std::string &text)
{
  // Create a refined_string_exprt using CBMC's string solver infrastructure.
  // The string is represented as {length, content_pointer}.
  // For constant strings, we create an array symbol and point to it.
  refined_string_typet str_type = typescript_string_type();

  // Create array for string content
  static unsigned str_arr_ctr = 0;
  std::string arr_name = "__ts_str_" + std::to_string(str_arr_ctr++);
  std::string arr_qname = "typescript::" + arr_name;
  irep_idt arr_id{arr_qname};

  array_typet arr_type{
    unsignedbv_typet{16},
    from_integer(text.size() > 0 ? text.size() : 1, signedbv_typet{32})};

  exprt::operandst chars;
  for(char c : text)
    chars.push_back(
      from_integer(static_cast<unsigned char>(c), unsignedbv_typet{16}));
  if(chars.empty())
    chars.push_back(from_integer(0, unsignedbv_typet{16}));

  if(symbol_table.lookup(arr_id) == nullptr)
  {
    symbolt arr_sym{arr_id, arr_type, "typescript"};
    arr_sym.base_name = arr_name;
    arr_sym.is_lvalue = true;
    arr_sym.is_state_var = true;
    arr_sym.is_static_lifetime = true;
    arr_sym.value = array_exprt{std::move(chars), arr_type};
    symbol_table.add(arr_sym);
  }

  // Create pointer to the array
  exprt content_ptr = address_of_exprt{index_exprt{
    symbol_table.lookup_ref(arr_id).symbol_expr(),
    from_integer(0, signedbv_typet{32})}};

  exprt length =
    from_integer(static_cast<int>(text.size()), signedbv_typet{32});

  return refined_string_exprt{length, content_ptr, str_type};
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
