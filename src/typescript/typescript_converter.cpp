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
  // Object literal types: { x: number; y: number; }
  if(ts_type.size() > 2 && ts_type[0] == '{' && ts_type.back() == '}')
  {
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
  // Union types: string | null, number | undefined, etc.
  if(ts_type.find(" | ") != std::string::npos)
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
    std::size_t max_len = 64;
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
        if(expr.id() == ID_symbol)
        {
          auto it =
            string_constants.find(to_symbol_expr(expr).get_identifier());
          if(it != string_constants.end())
            result += it->second;
          else
            all_const = false;
        }
        else if(
          is_typescript_string_type(expr.type()) && expr.id() == ID_struct &&
          expr.operands().size() >= 2)
        {
          // Extract from literal
          mp_integer len;
          if(
            expr.operands()[0].is_constant() &&
            !to_integer(to_constant_expr(expr.operands()[0]), len))
          {
            const exprt &data = expr.operands()[1];
            for(mp_integer i = 0; i < len; ++i)
            {
              auto idx = i.to_ulong();
              if(
                idx < data.operands().size() &&
                data.operands()[idx].is_constant())
              {
                mp_integer ch;
                if(!to_integer(to_constant_expr(data.operands()[idx]), ch))
                  result += static_cast<char>(ch.to_ulong());
              }
            }
          }
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
      std::string pname =
        json_string(json_member(json_member(prop, "name"), "text"));
      exprt val = convert_expression(json_member(prop, "initializer"));
      if(val.is_nil())
        continue;
      components.push_back(struct_typet::componentt{pname, val.type()});
      fields.push_back(val);
    }
    struct_typet st{components};
    return struct_exprt{std::move(fields), st};
  }
  // ES2024 sec-array-initializer
  if(kind == "ArrayLiteralExpression")
  {
    const jsont &elts = json_member(node, "elements");
    if(!elts.is_array() || to_json_array(elts).empty())
      return nil_exprt{};
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
    std::size_t max_len = 64;
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
    // Return a string constant based on the expression's type
    exprt operand = convert_expression(json_member(node, "expression"));
    std::string ts_type =
      json_string(json_member(json_member(node, "expression"), "_type"));
    std::string typeof_result = "object"; // default
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
  // ES2024 sec-spread-element
  if(kind == "SpreadElement")
    return convert_expression(json_member(node, "expression"));
  log.warning() << "Unsupported expression: " << kind << messaget::eom;
  return nil_exprt{};
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
    return sym->symbol_expr();

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
  if(left.type() != right.type())
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
      // Try constant evaluation
      auto ext = [this](const exprt &e) -> std::string
      {
        if(e.id() == ID_symbol)
        {
          auto it = string_constants.find(to_symbol_expr(e).get_identifier());
          if(it != string_constants.end())
            return it->second;
        }
        // For refined_string_exprt: {length, content_ptr}
        if(
          e.id() == ID_struct && e.operands().size() >= 2 &&
          e.operands()[0].is_constant())
        {
          mp_integer len;
          if(!to_integer(to_constant_expr(e.operands()[0]), len))
          {
            // Content pointer: address_of(arr[0])
            const exprt &ptr = e.operands()[1];
            if(
              ptr.id() == ID_address_of && ptr.operands()[0].id() == ID_index &&
              ptr.operands()[0].operands()[0].id() == ID_symbol)
            {
              irep_idt arr_id = to_symbol_expr(ptr.operands()[0].operands()[0])
                                  .get_identifier();
              const symbolt *arr_sym = symbol_table.lookup(arr_id);
              if(arr_sym != nullptr && !arr_sym->value.is_nil())
              {
                std::string s;
                const exprt &arr = arr_sym->value;
                for(mp_integer i = 0; i < len; ++i)
                {
                  auto idx = i.to_ulong();
                  if(
                    idx < arr.operands().size() &&
                    arr.operands()[idx].is_constant())
                  {
                    mp_integer ch;
                    if(!to_integer(to_constant_expr(arr.operands()[idx]), ch))
                      s += static_cast<char>(ch.to_ulong());
                  }
                }
                return s;
              }
            }
          }
        }
        return "";
      };
      std::string ls = ext(left), rs = ext(right);
      if(!ls.empty() || !rs.empty())
        return convert_string_literal_from_text(ls + rs);
      return side_effect_expr_nondett{
        typescript_string_type(), source_locationt{}};
    }
    return plus_exprt{left, right};
  }
  // ES2024 sec-subtraction-operator-minus
  if(op == "MinusToken")
    return minus_exprt{left, right};
  // ES2024 sec-multiplicative-operators
  if(op == "AsteriskToken")
    return mult_exprt{left, right};
  if(op == "SlashToken")
  {
    if(left.type().id() == ID_floatbv)
    {
      exprt rm = symbol_exprt{"__CPROVER_rounding_mode", signedbv_typet{32}};
      ieee_float_op_exprt result{left, ID_floatbv_div, right, rm};
      result.type() = left.type();
      return std::move(result);
    }
    return div_exprt{left, right};
  }
  // ES2024 sec-numeric-types-number-remainder
  if(op == "PercentToken")
  {
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
      auto ext = [this](const exprt &e) -> std::string
      {
        if(e.id() == ID_symbol)
        {
          auto it = string_constants.find(to_symbol_expr(e).get_identifier());
          if(it != string_constants.end())
            return "S:" + it->second;
        }
        if(
          e.id() == ID_struct && e.operands().size() >= 2 &&
          e.operands()[0].is_constant())
        {
          mp_integer len;
          if(!to_integer(to_constant_expr(e.operands()[0]), len))
          {
            // Check for refined_string: {length, address_of(arr[0])}
            const exprt &ptr = e.operands()[1];
            if(
              ptr.id() == ID_address_of && ptr.operands()[0].id() == ID_index &&
              ptr.operands()[0].operands()[0].id() == ID_symbol)
            {
              irep_idt aid = to_symbol_expr(ptr.operands()[0].operands()[0])
                               .get_identifier();
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
                    if(!to_integer(
                         to_constant_expr(as->value.operands()[idx]), ch))
                      s += static_cast<char>(ch.to_ulong());
                  }
                }
                return "S:" + s;
              }
            }
          }
        }
        return "";
      };
      std::string ls = ext(left), rs = ext(right);
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

  // ES2024 sec-nullish-coalescing: ??
  if(op == "QuestionQuestionToken")
  {
    // x ?? y → x !== null && x !== undefined ? x : y
    // For numbers: x is never null, so just return x
    return left;
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
      if(obj_expr.id() == ID_symbol)
      {
        auto it =
          string_constants.find(to_symbol_expr(obj_expr).get_identifier());
        if(it != string_constants.end())
          sv = it->second;
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
          if(av.id() == ID_symbol)
          {
            auto it =
              string_constants.find(to_symbol_expr(av).get_identifier());
            if(it != string_constants.end())
            {
              str_args.push_back(it->second);
              continue;
            }
          }
          if(
            av.id() == ID_struct && av.operands().size() >= 2 &&
            av.operands()[0].is_constant() &&
            av.operands()[1].id() == ID_address_of)
          {
            // Extract from refined_string_exprt
            mp_integer len;
            if(!to_integer(to_constant_expr(av.operands()[0]), len))
            {
              const exprt &ptr = av.operands()[1];
              if(
                ptr.operands()[0].id() == ID_index &&
                ptr.operands()[0].operands()[0].id() == ID_symbol)
              {
                const symbolt *as = symbol_table.lookup(
                  to_symbol_expr(ptr.operands()[0].operands()[0])
                    .get_identifier());
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
                      if(!to_integer(
                           to_constant_expr(as->value.operands()[idx]), ch))
                        s += static_cast<char>(ch.to_ulong());
                    }
                  }
                  str_args.push_back(s);
                  continue;
                }
              }
            }
          }
          // Try number
          if(av.is_constant() && av.type().id() == ID_floatbv)
          {
            ieee_floatt fv{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            fv.from_expr(to_constant_expr(av));
            num_args.push_back(
              static_cast<int>(std::stod(fv.to_ansi_c_string())));
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
            side_effect_expr_function_callt call{
              symbol_exprt{cb_id, symbol_table.lookup_ref(cb_id).type},
              {data.operands()[idx]},
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
        std::size_t max_len = 64;
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
        std::size_t max_len = 64;
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
    if(func_name == "__CPROVER_assume")
    {
      // Handled at statement level
      return nil_exprt{};
    }

    // Regular function call
    irep_idt func_id{"typescript::" + func_name};
    const symbolt *sym = symbol_table.lookup(func_id);
    if(sym != nullptr && sym->type.id() == ID_code)
    {
      const code_typet &func_type = to_code_type(sym->type);
      exprt::operandst arguments;
      if(args.is_array())
      {
        for(const auto &arg : to_json_array(args))
          arguments.push_back(convert_expression(arg));
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
        if(arguments.size() > regular_count)
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
          std::size_t max_len = 64;
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

codet typescript_convertert::convert_statement(const jsont &node)
{
  std::string kind = json_string(json_member(node, "_kind"));

  // ES2024 sec-variable-statement / sec-let-and-const-declarations
  if(kind == "FirstStatement" || kind == "VariableStatement")
    return convert_variable_statement(node);
  if(kind == "VariableDeclarationList")
  {
    // Wrap in a pseudo-statement for the variable handler
    // The for-loop initializer is a bare VariableDeclarationList
    jsont wrapper;
    // Create a wrapper with declarationList field
    // Actually, just handle it directly
    const jsont &declarations = json_member(node, "declarations");
    if(!declarations.is_array())
      return code_skipt{};
    code_blockt block;
    for(const auto &decl : to_json_array(declarations))
    {
      std::string var_name =
        json_string(json_member(json_member(decl, "name"), "text"));
      // Check for arrow/function expression BEFORE creating variable
      const jsont &init = json_member(decl, "initializer");
      if(init.is_object())
      {
        std::string init_kind = json_string(json_member(init, "_kind"));
        if(init_kind == "ArrowFunction" || init_kind == "FunctionExpression")
        {
          convert_function_declaration_with_name(init, var_name);
          continue;
        }
      }
      std::string ts_type = json_string(json_member(decl, "_type"));
      typet var_type = convert_type(ts_type);
      std::string qualified =
        "typescript::" +
        (current_function.empty() ? "" : current_function + "::") + var_name;
      irep_idt sym_id{qualified};
      {
        symbolt new_sym{sym_id, var_type, "typescript"};
        new_sym.base_name = var_name;
        new_sym.is_lvalue = true;
        new_sym.is_state_var = true;
        symbol_table.add(new_sym);
      }
      if(init.is_object())
      {
        exprt rhs = convert_expression(init);
        if(!rhs.is_nil())
        {
          const symbolt &sym = symbol_table.lookup_ref(sym_id);
          if(rhs.type() != sym.type)
            rhs = typecast_exprt(rhs, sym.type);
          // Flush pending stmts BEFORE assignment (constructor calls etc.)
          for(auto &s : pending_stmts)
            block.add(std::move(s));
          pending_stmts.clear();
          block.add(code_frontend_assignt{sym.symbol_expr(), rhs});
          // Track string constants
          {
            std::string sv = extract_string_value(rhs);
            if(!sv.empty())
              string_constants[sym_id] = sv.substr(2);
          }
          // Set symbol value for constant arrays (enables spread)
          {
            const exprt &val =
              rhs.id() == ID_typecast ? to_typecast_expr(rhs).op() : rhs;
            if(
              val.id() == ID_struct && val.type().id() == ID_struct &&
              to_struct_type(val.type()).get_tag() == "typescript_array")
            {
              symbolt *ws = symbol_table.get_writeable(sym_id);
              if(ws != nullptr)
                ws->value = val;
            }
          }
        }
      }
    }
    if(block.statements().size() == 1)
      return block.statements().front();
    return std::move(block);
  }

  if(kind == "ExpressionStatement")
    return convert_expression_statement(node);

  // ES2024 sec-if-statement
  if(kind == "IfStatement")
    return convert_if_statement(node);

  // ES2024 sec-while-statement
  if(kind == "WhileStatement")
    return convert_while_statement(node);

  // ES2024 sec-do-while-statement
  if(kind == "DoStatement")
  {
    exprt cond = convert_expression(json_member(node, "expression"));
    codet body = convert_statement(json_member(node, "statement"));
    return code_dowhilet{std::move(cond), std::move(body)};
  }
  // ES2024 sec-for-statement
  if(kind == "ForStatement")
    return convert_for_statement(node);

  // ES2024 sec-return-statement
  if(kind == "ReturnStatement")
    return convert_return_statement(node);

  if(kind == "Block")
    return convert_block(node);

  // ES2024 sec-function-definitions
  if(kind == "FunctionDeclaration")
  {
    convert_function_declaration(node);
    return code_skipt{};
  }

  // ES2024 sec-switch-statement
  if(kind == "SwitchStatement")
  {
    exprt disc = convert_expression(json_member(node, "expression"));
    const jsont &case_block = json_member(node, "caseBlock");
    const jsont &clauses = json_member(case_block, "clauses");
    if(disc.is_nil() || !clauses.is_array())
      return code_skipt{};
    // Convert to if-else chain (process default first, then cases in reverse)
    const auto &clause_arr = to_json_array(clauses);
    // Find default clause
    codet default_body = code_skipt{};
    for(const auto &clause : clause_arr)
    {
      if(json_string(json_member(clause, "_kind")) == "DefaultClause")
      {
        code_blockt body;
        const jsont &stmts = json_member(clause, "statements");
        if(stmts.is_array())
          for(const auto &s : to_json_array(stmts))
            body.add(convert_statement(s));
        default_body = std::move(body);
        break;
      }
    }
    // Build if-else chain from last case to first
    codet result = std::move(default_body);
    std::vector<std::reference_wrapper<const jsont>> cases;
    for(const auto &clause : clause_arr)
      if(json_string(json_member(clause, "_kind")) != "DefaultClause")
        cases.push_back(std::cref(clause));
    for(auto it = cases.rbegin(); it != cases.rend(); ++it)
    {
      const jsont &clause = it->get();
      code_blockt body;
      const jsont &stmts = json_member(clause, "statements");
      if(stmts.is_array())
        for(const auto &s : to_json_array(stmts))
          body.add(convert_statement(s));
      exprt case_val = convert_expression(json_member(clause, "expression"));
      if(!case_val.is_nil())
      {
        if(case_val.type() != disc.type())
          case_val = typecast_exprt{case_val, disc.type()};
        exprt cond = disc.type().id() == ID_floatbv
                       ? exprt{ieee_float_equal_exprt{disc, case_val}}
                       : exprt{equal_exprt{disc, case_val}};
        result = code_ifthenelset{cond, std::move(body), std::move(result)};
      }
    }
    return result;
  }

  // TSH: Enums
  if(kind == "EnumDeclaration")
  {
    std::string enum_name =
      json_string(json_member(json_member(node, "name"), "text"));
    const jsont &members = json_member(node, "members");
    if(members.is_array())
    {
      int value = 0;
      for(const auto &m : to_json_array(members))
      {
        std::string mname =
          json_string(json_member(json_member(m, "name"), "text"));
        // Check for explicit initializer
        const jsont &init = json_member(m, "initializer");
        if(init.is_object())
        {
          exprt val = convert_expression(init);
          if(val.is_constant())
          {
            mp_integer iv;
            if(!to_integer(to_constant_expr(val), iv))
              value = iv.to_long();
          }
        }
        // Create symbol: EnumName.MemberName = value
        std::string qn = "typescript::" + enum_name + "." + mname;
        irep_idt sid{qn};
        if(symbol_table.lookup(sid) == nullptr)
        {
          symbolt s{sid, double_type(), "typescript"};
          s.base_name = enum_name + "." + mname;
          s.is_lvalue = true;
          s.is_state_var = true;
          s.is_static_lifetime = true;
          // Store as float constant
          uint64_t bits;
          double dval = static_cast<double>(value);
          std::memcpy(&bits, &dval, sizeof(bits));
          s.value = constant_exprt{
            integer2bvrep(mp_integer{std::to_string(bits).c_str()}, 64),
            double_type()};
          symbol_table.add(s);
        }
        value++;
      }
    }
    // Generate initialization code for enum members
    code_blockt enum_init;
    for(const auto &m : to_json_array(members))
    {
      std::string mname =
        json_string(json_member(json_member(m, "name"), "text"));
      std::string qn = "typescript::" + enum_name + "." + mname;
      const symbolt *s = symbol_table.lookup(irep_idt{qn});
      if(s != nullptr && !s->value.is_nil())
        enum_init.add(code_frontend_assignt{s->symbol_expr(), s->value});
    }
    return std::move(enum_init);
  }
  // TSH: Object Types.md — interface declarations (type-only, no runtime code)
  if(kind == "InterfaceDeclaration" || kind == "TypeAliasDeclaration")
    return code_skipt{};

  // ES2024 sec-class-definitions
  if(kind == "ClassDeclaration")
  {
    std::string cls_name =
      json_string(json_member(json_member(node, "name"), "text"));
    if(cls_name.empty())
      return code_skipt{};
    // Build struct type from property declarations
    struct_typet cls_type;
    cls_type.set_tag("typescript_class_" + cls_name);

    // Check for inheritance: class Dog extends Animal
    std::string parent_name;
    const jsont &heritage = json_member(node, "heritage");
    if(heritage.is_array())
    {
      for(const auto &clause : to_json_array(heritage))
      {
        const jsont &children = json_member(clause, "_children");
        if(children.is_array())
        {
          for(const auto &child : to_json_array(children))
          {
            const jsont &ch2 = json_member(child, "_children");
            if(ch2.is_array())
            {
              for(const auto &id : to_json_array(ch2))
              {
                std::string t = json_string(json_member(id, "text"));
                if(!t.empty())
                  parent_name = t;
              }
            }
          }
        }
      }
    }
    // Copy parent fields
    if(!parent_name.empty())
    {
      auto pit = class_types.find(parent_name);
      if(pit != class_types.end())
      {
        for(const auto &comp : pit->second.components())
          cls_type.components().push_back(comp);
      }
    }

    const jsont &members = json_member(node, "members");
    if(members.is_array())
    {
      for(const auto &m : to_json_array(members))
      {
        std::string mk = json_string(json_member(m, "_kind"));
        if(mk == "PropertyDeclaration")
        {
          std::string pname =
            json_string(json_member(json_member(m, "name"), "text"));
          std::string ptype = json_string(json_member(m, "_type"));
          cls_type.components().push_back(
            struct_typet::componentt{pname, convert_type(ptype)});
        }
      }
    }
    // Register class type
    class_types[cls_name] = cls_type;
    std::string saved_class = current_class;
    current_class = cls_name;
    // Process constructor and methods
    if(members.is_array())
    {
      for(const auto &m : to_json_array(members))
      {
        std::string mk = json_string(json_member(m, "_kind"));
        if(mk == "Constructor")
        {
          // Constructor: cls_name::__init__(this_ptr, params...)
          std::string ctor_name = cls_name + "::__init__";
          code_typet::parameterst params;
          // this pointer
          code_typet::parametert this_param{pointer_typet{cls_type, 64}};
          this_param.set_identifier("typescript::" + ctor_name + "::this");
          this_param.set_base_name("this");
          params.push_back(this_param);
          // other params
          const jsont &ctor_params = json_member(m, "parameters");
          if(ctor_params.is_array())
          {
            for(const auto &p : to_json_array(ctor_params))
            {
              std::string pn =
                json_string(json_member(json_member(p, "name"), "text"));
              std::string pt = json_string(json_member(p, "_type"));
              code_typet::parametert cp{convert_type(pt)};
              cp.set_identifier("typescript::" + ctor_name + "::" + pn);
              cp.set_base_name(pn);
              params.push_back(cp);
            }
          }
          code_typet ft{params, empty_typet{}};
          irep_idt fid{"typescript::" + ctor_name};
          symbolt fs{fid, ft, "typescript"};
          fs.base_name = ctor_name;
          fs.is_lvalue = true;
          // Create param symbols
          for(const auto &p : params)
          {
            irep_idt pid = p.get_identifier();
            if(symbol_table.lookup(pid) == nullptr)
            {
              symbolt ps{pid, p.type(), "typescript"};
              ps.base_name = id2string(p.get_base_name());
              ps.is_parameter = true;
              ps.is_lvalue = true;
              ps.is_state_var = true;
              symbol_table.add(ps);
            }
          }
          // Convert body
          const jsont &body = json_member(m, "body");
          if(body.is_object())
          {
            std::string saved = current_function;
            current_function = ctor_name;
            fs.value = convert_block(body);
            current_function = saved;
          }
          if(symbol_table.lookup(fid) == nullptr)
            symbol_table.add(fs);
        }
        else if(mk == "MethodDeclaration")
        {
          std::string mname =
            json_string(json_member(json_member(m, "name"), "text"));
          std::string full_name = cls_name + "::" + mname;
          std::string ret_str = json_string(json_member(m, "_returnType"));
          typet ret_type =
            ret_str.empty() ? empty_typet{} : convert_type(ret_str);
          code_typet::parameterst params;
          code_typet::parametert this_param{pointer_typet{cls_type, 64}};
          this_param.set_identifier("typescript::" + full_name + "::this");
          this_param.set_base_name("this");
          params.push_back(this_param);
          const jsont &mparams = json_member(m, "parameters");
          if(mparams.is_array())
          {
            for(const auto &p : to_json_array(mparams))
            {
              std::string pn =
                json_string(json_member(json_member(p, "name"), "text"));
              std::string pt = json_string(json_member(p, "_type"));
              code_typet::parametert cp{convert_type(pt)};
              cp.set_identifier("typescript::" + full_name + "::" + pn);
              cp.set_base_name(pn);
              params.push_back(cp);
            }
          }
          code_typet ft{params, ret_type};
          irep_idt fid{"typescript::" + full_name};
          symbolt fs{fid, ft, "typescript"};
          fs.base_name = full_name;
          fs.is_lvalue = true;
          for(const auto &p : params)
          {
            irep_idt pid = p.get_identifier();
            if(symbol_table.lookup(pid) == nullptr)
            {
              symbolt ps{pid, p.type(), "typescript"};
              ps.base_name = id2string(p.get_base_name());
              ps.is_parameter = true;
              ps.is_lvalue = true;
              ps.is_state_var = true;
              symbol_table.add(ps);
            }
          }
          const jsont &body = json_member(m, "body");
          if(body.is_object())
          {
            std::string saved = current_function;
            current_function = full_name;
            fs.value = convert_block(body);
            current_function = saved;
          }
          if(symbol_table.lookup(fid) == nullptr)
            symbol_table.add(fs);
        }
      }
    }
    current_class = saved_class;
    return code_skipt{};
  }

  // ES2024 sec-for-in-and-for-of-statements
  if(kind == "ForOfStatement")
  {
    // Convert: for(const x of arr) { body }
    // → let __i = 0; while(__i < arr.length) { const x = arr.data[__i]; body; __i++; }
    code_blockt block;
    // Get the array expression
    exprt arr = convert_expression(json_member(node, "expression"));
    if(arr.is_nil())
      return code_skipt{};
    // Create iterator variable
    static unsigned forit_ctr = 0;
    std::string it_name = "__forit_" + std::to_string(forit_ctr++);
    std::string it_qname =
      "typescript::" +
      (current_function.empty() ? "" : current_function + "::") + it_name;
    irep_idt it_id{it_qname};
    if(symbol_table.lookup(it_id) == nullptr)
    {
      symbolt it_sym{it_id, signedbv_typet{64}, "typescript"};
      it_sym.base_name = it_name;
      it_sym.is_lvalue = true;
      it_sym.is_state_var = true;
      symbol_table.add(it_sym);
    }
    symbol_exprt it_var = symbol_table.lookup_ref(it_id).symbol_expr();
    block.add(
      code_frontend_assignt{it_var, from_integer(0, signedbv_typet{64})});
    // Get loop variable name
    const jsont &init_node = json_member(node, "initializer");
    std::string loop_var;
    if(is_kind(init_node, "VariableDeclarationList"))
    {
      const jsont &decls = json_member(init_node, "declarations");
      if(decls.is_array() && !to_json_array(decls).empty())
        loop_var = json_string(json_member(
          json_member(*to_json_array(decls).begin(), "name"), "text"));
    }
    // Create loop variable
    typet elem_type = double_type();
    if(arr.type().id() == ID_struct)
    {
      const auto &st = to_struct_type(arr.type());
      if(st.has_component("data"))
        elem_type =
          to_array_type(st.get_component("data").type()).element_type();
    }
    std::string lv_qname =
      "typescript::" +
      (current_function.empty() ? "" : current_function + "::") + loop_var;
    irep_idt lv_id{lv_qname};
    if(!loop_var.empty() && symbol_table.lookup(lv_id) == nullptr)
    {
      symbolt lv_sym{lv_id, elem_type, "typescript"};
      lv_sym.base_name = loop_var;
      lv_sym.is_lvalue = true;
      lv_sym.is_state_var = true;
      symbol_table.add(lv_sym);
    }
    // Build while loop
    exprt arr_len = member_exprt{arr, "length", signedbv_typet{64}};
    exprt cond = binary_relation_exprt{it_var, ID_lt, arr_len};
    code_blockt loop_body;
    if(!loop_var.empty())
    {
      const symbolt &lv = symbol_table.lookup_ref(lv_id);
      if(arr.type().id() == ID_struct)
      {
        const auto &st = to_struct_type(arr.type());
        if(st.has_component("data"))
        {
          exprt data =
            member_exprt{arr, "data", st.get_component("data").type()};
          loop_body.add(
            code_frontend_assignt{lv.symbol_expr(), index_exprt{data, it_var}});
        }
      }
    }
    loop_body.add(convert_statement(json_member(node, "statement")));
    loop_body.add(code_frontend_assignt{
      it_var, plus_exprt{it_var, from_integer(1, signedbv_typet{64})}});
    block.add(code_whilet{cond, std::move(loop_body)});
    return std::move(block);
  }
  // ES2024 sec-try-statement
  if(kind == "TryStatement")
  {
    code_blockt block;
    // Convert try block
    const jsont &try_block = json_member(node, "tryBlock");
    if(try_block.is_object())
      block.add(convert_block(try_block));
    // Convert catch clause (simplified: always execute catch after try)
    const jsont &catch_clause = json_member(node, "catchClause");
    if(catch_clause.is_object())
    {
      const jsont &catch_block = json_member(catch_clause, "block");
      if(catch_block.is_object())
      {
        // Create catch variable if present
        const jsont &var_decl =
          json_member(catch_clause, "variableDeclaration");
        if(var_decl.is_object())
        {
          std::string vname =
            json_string(json_member(json_member(var_decl, "name"), "text"));
          if(!vname.empty())
          {
            std::string qn =
              "typescript::" +
              (current_function.empty() ? "" : current_function + "::") + vname;
            if(symbol_table.lookup(irep_idt{qn}) == nullptr)
            {
              symbolt vs{irep_idt{qn}, double_type(), "typescript"};
              vs.base_name = vname;
              vs.is_lvalue = true;
              vs.is_state_var = true;
              symbol_table.add(vs);
            }
          }
        }
        block.add(convert_block(catch_block));
      }
    }
    return std::move(block);
  }
  // ES2024 sec-throw-statement
  if(kind == "ThrowStatement")
    return code_skipt{}; // simplified: throw is a no-op for now
  log.warning() << "Unsupported statement kind: " << kind << messaget::eom;
  return code_skipt{};
}

// ES2024 sec-let-and-const-declarations
codet typescript_convertert::convert_variable_statement(const jsont &node)
{
  const jsont &decl_list = json_member(node, "declarationList");
  const jsont &declarations = json_member(decl_list, "declarations");

  if(!declarations.is_array())
    return code_skipt{};

  code_blockt block;
  for(const auto &decl : to_json_array(declarations))
  {
    const jsont &name_node = json_member(decl, "name");
    std::string name_kind = json_string(json_member(name_node, "_kind"));
    // Handle destructuring: const { x, y } = point
    if(name_kind == "ObjectBindingPattern")
    {
      const jsont &init = json_member(decl, "initializer");
      if(!init.is_object())
        continue;
      exprt rhs = convert_expression(init);
      if(rhs.is_nil())
        continue;
      const jsont &elements = json_member(name_node, "elements");
      if(elements.is_array() && rhs.type().id() == ID_struct)
      {
        const auto &st = to_struct_type(rhs.type());
        for(const auto &elem : to_json_array(elements))
        {
          std::string prop =
            json_string(json_member(json_member(elem, "name"), "text"));
          if(prop.empty() || !st.has_component(prop))
            continue;
          typet pt = st.get_component(prop).type();
          std::string qn =
            "typescript::" +
            (current_function.empty() ? "" : current_function + "::") + prop;
          irep_idt pid{qn};
          if(symbol_table.lookup(pid) == nullptr)
          {
            symbolt ps{pid, pt, "typescript"};
            ps.base_name = prop;
            ps.is_lvalue = true;
            ps.is_state_var = true;
            ps.is_static_lifetime = current_function.empty();
            symbol_table.add(ps);
          }
          block.add(code_frontend_assignt{
            symbol_table.lookup_ref(pid).symbol_expr(),
            member_exprt{rhs, prop, pt}});
        }
      }
      continue;
    }
    // Handle array destructuring: const [a, b] = arr
    if(name_kind == "ArrayBindingPattern")
    {
      const jsont &init = json_member(decl, "initializer");
      if(!init.is_object())
        continue;
      exprt rhs = convert_expression(init);
      if(rhs.is_nil())
        continue;
      const jsont &elements = json_member(name_node, "elements");
      if(elements.is_array() && rhs.type().id() == ID_struct)
      {
        const auto &st = to_struct_type(rhs.type());
        if(st.has_component("data"))
        {
          exprt data =
            member_exprt{rhs, "data", st.get_component("data").type()};
          std::size_t idx = 0;
          for(const auto &elem : to_json_array(elements))
          {
            std::string ename =
              json_string(json_member(json_member(elem, "name"), "text"));
            if(ename.empty())
            {
              idx++;
              continue;
            }
            typet et =
              to_array_type(st.get_component("data").type()).element_type();
            std::string qn =
              "typescript::" +
              (current_function.empty() ? "" : current_function + "::") + ename;
            irep_idt eid{qn};
            if(symbol_table.lookup(eid) == nullptr)
            {
              symbolt es{eid, et, "typescript"};
              es.base_name = ename;
              es.is_lvalue = true;
              es.is_state_var = true;
              es.is_static_lifetime = current_function.empty();
              symbol_table.add(es);
            }
            block.add(code_frontend_assignt{
              symbol_table.lookup_ref(eid).symbol_expr(),
              index_exprt{data, from_integer(idx, signedbv_typet{64})}});
            idx++;
          }
        }
      }
      continue;
    }
    std::string var_name = json_string(json_member(name_node, "text"));
    // Check for arrow/function expression BEFORE creating variable
    const jsont &init_check = json_member(decl, "initializer");
    if(init_check.is_object())
    {
      std::string ik = json_string(json_member(init_check, "_kind"));
      if(ik == "ArrowFunction" || ik == "FunctionExpression")
      {
        convert_function_declaration_with_name(init_check, var_name);
        continue;
      }
    }
    std::string ts_type = json_string(json_member(decl, "_type"));
    typet var_type = convert_type(ts_type);

    std::string qualified =
      "typescript::" +
      (current_function.empty() ? "" : current_function + "::") + var_name;
    irep_idt sym_id{qualified};

    {
      symbolt new_sym{sym_id, var_type, "typescript"};
      new_sym.base_name = var_name;
      new_sym.location = get_location(decl);
      new_sym.is_lvalue = true;
      new_sym.is_state_var = true;
      new_sym.is_static_lifetime = current_function.empty();
      symbol_table.add(new_sym);
    }

    const jsont &init = json_member(decl, "initializer");
    if(!init.is_null() && !init.is_object())
    {
      // No initializer
    }
    else if(init.is_object())
    {
      exprt rhs = convert_expression(init);
      if(!rhs.is_nil())
      {
        const symbolt &sym = symbol_table.lookup_ref(sym_id);
        if(rhs.type() != sym.type)
          rhs = typecast_exprt{rhs, sym.type};
        // Flush pending stmts (constructor calls from NewExpression)
        for(auto &s : pending_stmts)
          block.add(std::move(s));
        pending_stmts.clear();
        code_frontend_assignt assign{sym.symbol_expr(), rhs};
        assign.add_source_location() = get_location(decl);
        block.add(std::move(assign));
        // Track string constants (works with refined_string_exprt)
        if(
          is_typescript_string_type(rhs.type()) && rhs.id() == ID_struct &&
          rhs.operands().size() >= 2 && rhs.operands()[0].is_constant())
        {
          mp_integer len;
          if(!to_integer(to_constant_expr(rhs.operands()[0]), len))
          {
            // refined_string: {length, address_of(arr[0])}
            const exprt &ptr = rhs.operands()[1];
            if(
              ptr.id() == ID_address_of && ptr.operands()[0].id() == ID_index &&
              ptr.operands()[0].operands()[0].id() == ID_symbol)
            {
              irep_idt aid = to_symbol_expr(ptr.operands()[0].operands()[0])
                               .get_identifier();
              const symbolt *as = symbol_table.lookup(aid);
              if(as && !as->value.is_nil())
              {
                std::string sv;
                for(mp_integer i = 0; i < len; ++i)
                {
                  auto idx = i.to_ulong();
                  if(
                    idx < as->value.operands().size() &&
                    as->value.operands()[idx].is_constant())
                  {
                    mp_integer ch;
                    if(!to_integer(
                         to_constant_expr(as->value.operands()[idx]), ch))
                      sv += static_cast<char>(ch.to_ulong());
                  }
                }
                string_constants[sym_id] = sv;
              }
            }
          }
        }
        // Set symbol value for constant arrays (enables spread)
        {
          const exprt &val =
            rhs.id() == ID_typecast ? to_typecast_expr(rhs).op() : rhs;
          if(
            val.id() == ID_struct && val.type().id() == ID_struct &&
            to_struct_type(val.type()).get_tag() == "typescript_array")
          {
            symbolt *ws = symbol_table.get_writeable(sym_id);
            if(ws != nullptr)
              ws->value = val;
          }
        }
      }
    }
  }

  if(block.statements().size() == 1)
    return block.statements().front();
  return std::move(block);
}

codet typescript_convertert::convert_expression_statement(const jsont &node)
{
  const jsont &expr_node = json_member(node, "expression");
  std::string expr_kind = json_string(json_member(expr_node, "_kind"));
  if(expr_kind == "CallExpression")
  {
    const jsont &callee_node = json_member(expr_node, "expression");
    std::string callee_kind = json_string(json_member(callee_node, "_kind"));
    if(callee_kind == "PropertyAccessExpression")
    {
      const jsont &obj_node = json_member(callee_node, "expression");
      std::string obj_text = json_string(json_member(obj_node, "text"));
      const jsont &method_node = json_member(callee_node, "name");
      std::string method_text = json_string(json_member(method_node, "text"));
      if(obj_text == "console" && method_text == "log")
        return code_skipt{};
      if(obj_text == "console" && method_text == "assert")
      {
        const jsont &call_args = json_member(expr_node, "arguments");
        if(call_args.is_array())
        {
          const auto &arr = to_json_array(call_args);
          if(!arr.empty())
          {
            exprt cond = convert_expression(*arr.begin());
            if(!cond.is_nil())
            {
              if(cond.type().id() != ID_bool)
                cond = typecast_exprt{cond, bool_typet{}};
              code_assertt assertion{cond};
              assertion.add_source_location() = get_location(expr_node);
              return std::move(assertion);
            }
          }
        }
        return code_skipt{};
      }
    }
    // Handle __CPROVER_assume
    if(callee_kind == "Identifier")
    {
      std::string fn = json_string(json_member(callee_node, "text"));
      if(fn == "__CPROVER_assume")
      {
        const jsont &call_args = json_member(expr_node, "arguments");
        if(call_args.is_array())
        {
          const auto &arr = to_json_array(call_args);
          if(!arr.empty())
          {
            exprt cond = convert_expression(*arr.begin());
            if(!cond.is_nil())
            {
              if(cond.type().id() != ID_bool)
                cond = typecast_exprt{cond, bool_typet{}};
              return code_assumet{cond};
            }
          }
        }
        return code_skipt{};
      }
    }
  }
  // Handle postfix increment/decrement: i++ → i = i + 1
  if(expr_kind == "PostfixUnaryExpression")
  {
    std::string op = json_string(json_member(expr_node, "operator"));
    exprt operand = convert_expression(json_member(expr_node, "operand"));
    if(!operand.is_nil())
    {
      exprt one = from_integer(1, operand.type());
      exprt new_val = (op == "PlusPlusToken")
                        ? exprt{plus_exprt{operand, one}}
                        : exprt{minus_exprt{operand, one}};
      return code_frontend_assignt{operand, new_val};
    }
  }
  // Handle prefix increment/decrement: ++i → i = i + 1
  if(expr_kind == "PrefixUnaryExpression")
  {
    std::string op = json_string(json_member(expr_node, "operator"));
    if(op == "PlusPlusToken" || op == "MinusMinusToken")
    {
      exprt operand = convert_expression(json_member(expr_node, "operand"));
      if(!operand.is_nil())
      {
        exprt one = from_integer(1, operand.type());
        exprt new_val = (op == "PlusPlusToken")
                          ? exprt{plus_exprt{operand, one}}
                          : exprt{minus_exprt{operand, one}};
        return code_frontend_assignt{operand, new_val};
      }
    }
  }
  // Handle compound assignments: x += 1 → x = x + 1
  if(expr_kind == "BinaryExpression")
  {
    std::string op = json_string(json_member(expr_node, "operator"));
    if(
      op == "FirstCompoundAssignment" || op == "PlusEqualsToken" ||
      op == "MinusEqualsToken" || op == "AsteriskEqualsToken" ||
      op == "SlashEqualsToken" || op == "PercentEqualsToken" ||
      op == "EqualsToken" || op == "FirstAssignment")
    {
      exprt lhs = convert_expression(json_member(expr_node, "left"));
      exprt rhs = convert_expression(json_member(expr_node, "right"));
      if(!lhs.is_nil() && !rhs.is_nil())
      {
        exprt new_val = rhs;
        if(op != "EqualsToken" && op != "FirstAssignment")
        {
          if(lhs.type() != rhs.type())
            rhs = typecast_exprt{rhs, lhs.type()};
          if(op == "FirstCompoundAssignment" || op == "PlusEqualsToken")
            new_val = plus_exprt{lhs, rhs};
          else if(op == "MinusEqualsToken")
            new_val = minus_exprt{lhs, rhs};
          else if(op == "AsteriskEqualsToken")
            new_val = mult_exprt{lhs, rhs};
          else if(op == "SlashEqualsToken")
            new_val = div_exprt{lhs, rhs};
          else
            new_val = mod_exprt{lhs, rhs};
        }
        if(new_val.type() != lhs.type())
          new_val = typecast_exprt{new_val, lhs.type()};
        return code_frontend_assignt{lhs, new_val};
      }
    }
  }
  // Fallback: evaluate expression
  exprt e = convert_expression(expr_node);
  if(!pending_stmts.empty())
  {
    code_blockt block;
    for(auto &s : pending_stmts)
      block.add(std::move(s));
    pending_stmts.clear();
    if(!e.is_nil())
      block.add(code_expressiont{e});
    return std::move(block);
  }
  if(e.is_nil())
    return code_skipt{};
  return code_expressiont{e};
}
// ES2024 sec-if-statement
codet typescript_convertert::convert_if_statement(const jsont &node)
{
  exprt cond = convert_expression(json_member(node, "expression"));
  if(cond.is_nil())
    return code_skipt{};
  if(cond.type().id() != ID_bool)
    cond = typecast_exprt{cond, bool_typet{}};

  codet then_code = convert_statement(json_member(node, "thenStatement"));

  const jsont &else_node = json_member(node, "elseStatement");
  if(else_node.is_object())
  {
    codet else_code = convert_statement(else_node);
    return code_ifthenelset{cond, std::move(then_code), std::move(else_code)};
  }
  return code_ifthenelset{cond, std::move(then_code)};
}

// ES2024 sec-while-statement
codet typescript_convertert::convert_while_statement(const jsont &node)
{
  exprt cond = convert_expression(json_member(node, "expression"));
  if(cond.is_nil())
    return code_skipt{};
  if(cond.type().id() != ID_bool)
    cond = typecast_exprt{cond, bool_typet{}};

  codet body = convert_statement(json_member(node, "statement"));
  code_whilet loop{cond, std::move(body)};
  loop.add_source_location() = get_location(node);
  return std::move(loop);
}

// ES2024 sec-for-statement
codet typescript_convertert::convert_for_statement(const jsont &node)
{
  code_blockt block;

  // Initializer
  const jsont &init = json_member(node, "initializer");
  if(init.is_object())
    block.add(convert_statement(init));

  // Condition
  exprt cond = true_exprt{};
  const jsont &cond_node = json_member(node, "condition");
  if(cond_node.is_object())
  {
    cond = convert_expression(cond_node);
    if(cond.type().id() != ID_bool)
      cond = typecast_exprt{cond, bool_typet{}};
  }

  // Body + incrementor
  code_blockt loop_body;
  loop_body.add(convert_statement(json_member(node, "statement")));
  const jsont &inc = json_member(node, "incrementor");
  if(inc.is_object())
  {
    exprt inc_expr = convert_expression(inc);
    if(!inc_expr.is_nil())
      loop_body.add(code_expressiont{inc_expr});
  }

  code_whilet loop{cond, std::move(loop_body)};
  loop.add_source_location() = get_location(node);
  block.add(std::move(loop));
  return std::move(block);
}

// ES2024 sec-return-statement
codet typescript_convertert::convert_return_statement(const jsont &node)
{
  const jsont &expr = json_member(node, "expression");
  if(expr.is_object())
  {
    exprt val = convert_expression(expr);
    if(!val.is_nil())
    {
      // Typecast return value to match function's return type
      if(!current_function.empty())
      {
        irep_idt fid{"typescript::" + current_function};
        // Handle nested function names (Class::method)
        auto dot = current_function.find("::");
        if(dot == std::string::npos)
          fid = irep_idt{"typescript::" + current_function};
        const symbolt *fsym = symbol_table.lookup(fid);
        if(fsym != nullptr && fsym->type.id() == ID_code)
        {
          typet ret_type = to_code_type(fsym->type).return_type();
          if(ret_type.id() != ID_empty && val.type() != ret_type)
            val = typecast_exprt(val, ret_type);
        }
      }
      return code_frontend_returnt{val};
    }
  }
  return code_frontend_returnt{};
}

codet typescript_convertert::convert_block(const jsont &node)
{
  const jsont &stmts = json_member(node, "statements");
  if(!stmts.is_array())
    return code_skipt{};

  code_blockt block;
  for(const auto &stmt : to_json_array(stmts))
    block.add(convert_statement(stmt));
  return std::move(block);
}

// ES2024 sec-function-definitions
void typescript_convertert::convert_function_declaration(const jsont &node)
{
  std::string func_name =
    json_string(json_member(json_member(node, "name"), "text"));
  if(func_name.empty())
    return;
  convert_function_declaration_with_name(node, func_name);
}

void typescript_convertert::convert_function_declaration_with_name(
  const jsont &node,
  const std::string &func_name)
{
  // Get return type
  std::string ret_type_str = json_string(json_member(node, "_returnType"));
  typet ret_type =
    ret_type_str.empty() ? empty_typet{} : convert_type(ret_type_str);

  // Get parameters
  code_typet::parameterst params;
  const jsont &param_nodes = json_member(node, "parameters");
  if(param_nodes.is_array())
  {
    for(const auto &p : to_json_array(param_nodes))
    {
      std::string pname =
        json_string(json_member(json_member(p, "name"), "text"));
      std::string ptype_str = json_string(json_member(p, "_type"));
      typet ptype = convert_type(ptype_str);

      // Track rest parameters
      if(json_member(p, "isRest").is_true())
        rest_param_functions.insert(irep_idt{"typescript::" + func_name});

      code_typet::parametert param{ptype};
      param.set_identifier("typescript::" + func_name + "::" + pname);
      param.set_base_name(pname);
      params.push_back(param);
    }
  }

  code_typet func_type{params, ret_type};
  irep_idt func_id{"typescript::" + func_name};

  // Create function symbol
  symbolt func_sym{func_id, func_type, "typescript"};
  func_sym.base_name = func_name;
  func_sym.location = get_location(node);
  func_sym.is_lvalue = true;

  // Create parameter symbols
  for(const auto &param : params)
  {
    irep_idt pid = param.get_identifier();
    {
      symbolt psym{pid, param.type(), "typescript"};
      psym.base_name = id2string(param.get_base_name());
      psym.is_parameter = true;
      psym.is_lvalue = true;
      psym.is_state_var = true;
      symbol_table.add(psym);
    }
  }

  // Convert function body
  // Detect captured variables from enclosing scope
  // If this is a nested function (current_function is set), scan body for
  // identifiers that match enclosing function's parameters/locals
  std::vector<std::pair<std::string, typet>> captured_vars;
  if(!current_function.empty())
  {
    // Collect identifiers used in the body
    std::function<void(const jsont &)> scan = [&](const jsont &n)
    {
      if(!n.is_object())
        return;
      std::string k = json_string(json_member(n, "_kind"));
      if(k == "Identifier")
      {
        std::string text = json_string(json_member(n, "text"));
        if(text.empty())
          return;
        // Check if it's a parameter of THIS function
        bool is_local = false;
        for(const auto &p : params)
          if(id2string(p.get_base_name()) == text)
            is_local = true;
        if(is_local)
          return;
        // Check if it's a variable in the enclosing scope
        std::string outer_id = "typescript::" + current_function + "::" + text;
        const symbolt *outer_sym = symbol_table.lookup(irep_idt{outer_id});
        if(outer_sym != nullptr)
        {
          // Check not already captured
          bool already = false;
          for(const auto &cv : captured_vars)
            if(cv.first == text)
              already = true;
          if(!already)
            captured_vars.emplace_back(text, outer_sym->type);
        }
      }
      // Recurse into json children
      auto recurse_json = [&](const jsont &child)
      {
        if(child.is_object())
          scan(child);
        else if(child.is_array())
          for(const auto &c : to_json_array(child))
            scan(c);
      };
      // Check known child fields
      static const char *fields[] = {
        "expression",      "left",          "right",         "body",
        "statements",      "thenStatement", "elseStatement", "statement",
        "arguments",       "elements",      "properties",    "declarations",
        "declarationList", "initializer",   "condition",     "incrementor",
        "operand",         "head",          "templateSpans", "name",
        "members",         "parameters",    "_children",     nullptr};
      for(const char **f = fields; *f; ++f)
      {
        const jsont &child = json_member(n, *f);
        recurse_json(child);
      }
    };
    const jsont &body_node = json_member(node, "body");
    if(body_node.is_object())
      scan(body_node);

    // Add captured vars as extra parameters
    for(const auto &[cv_name, cv_type] : captured_vars)
    {
      code_typet::parametert cp{cv_type};
      cp.set_identifier("typescript::" + func_name + "::" + cv_name);
      cp.set_base_name(cv_name);
      params.push_back(cp);
      // Create parameter symbol
      irep_idt cpid{"typescript::" + func_name + "::" + cv_name};
      if(symbol_table.lookup(cpid) == nullptr)
      {
        symbolt cps{cpid, cv_type, "typescript"};
        cps.base_name = cv_name;
        cps.is_parameter = true;
        cps.is_lvalue = true;
        cps.is_state_var = true;
        symbol_table.add(cps);
      }
    }
    // Update function type with new params
    func_type = code_typet{params, ret_type};
    func_sym.type = func_type;
    // Store capture info for call sites
    if(!captured_vars.empty())
    {
      auto &cv = captured_var_map[func_id];
      for(const auto &[cv_name, cv_type] : captured_vars)
      {
        std::string outer_id =
          "typescript::" + current_function + "::" + cv_name;
        cv.emplace_back(cv_name, irep_idt{outer_id});
      }
    }
  }

  // Add function symbol to table BEFORE body conversion
  // (so return type can be looked up during body conversion)
  if(symbol_table.lookup(func_id) == nullptr)
    symbol_table.add(func_sym);
  else
  {
    // Update type if we added captured params
    symbol_table.get_writeable_ref(func_id).type = func_type;
  }

  // Store default parameter values
  {
    const jsont &fn_params = json_member(node, "parameters");
    if(fn_params.is_array())
    {
      std::size_t pi = 0;
      for(const auto &p : to_json_array(fn_params))
      {
        const jsont &def_init = json_member(p, "initializer");
        if(def_init.is_object())
        {
          exprt def_val = convert_expression(def_init);
          if(!def_val.is_nil())
            default_values[func_id][pi] = def_val;
        }
        pi++;
      }
    }
  }
  const jsont &body = json_member(node, "body");
  if(body.is_object())
  {
    std::string saved_function = current_function;
    current_function = func_name;
    std::string body_kind = json_string(json_member(body, "_kind"));
    codet body_code = code_skipt{};
    if(body_kind == "Block")
    {
      body_code = convert_block(body);
    }
    else
    {
      // Concise arrow function: body is an expression → wrap in return
      exprt expr = convert_expression(body);
      if(!expr.is_nil())
        body_code = code_frontend_returnt{expr};
    }
    current_function = saved_function;
    func_sym.value = body_code;
  }

  if(symbol_table.lookup(func_id) == nullptr)
    symbol_table.add(func_sym);
  else if(!func_sym.value.is_nil())
  {
    // Update existing symbol with body (e.g., declare → definition)
    symbol_table.get_writeable_ref(func_id).value = func_sym.value;
    symbol_table.get_writeable_ref(func_id).type = func_sym.type;
  }
}

// --- Module body ---

void typescript_convertert::convert_module_body(const jsont &statements)
{
  if(!statements.is_array())
    return;

  // Single pass: process all statements in order
  // Function declarations are registered AND their bodies converted.
  // Non-function statements go into __CPROVER__start body.
  code_blockt start_body;
  for(const auto &stmt : to_json_array(statements))
  {
    std::string kind = json_string(json_member(stmt, "_kind"));
    if(kind == "FunctionDeclaration")
    {
      convert_function_declaration(stmt);
      continue;
    }
    codet code = convert_statement(stmt);
    start_body.add(std::move(code));
  }

  // Create __CPROVER__start function
  // Initialize __CPROVER_rounding_mode at the start
  code_blockt full_body;
  {
    irep_idt rm_id{"__CPROVER_rounding_mode"};
    {
      symbolt rm_sym{rm_id, signedbv_typet{32}, "typescript"};
      rm_sym.base_name = "__CPROVER_rounding_mode";
      rm_sym.is_lvalue = true;
      rm_sym.is_state_var = true;
      rm_sym.is_static_lifetime = true;
      symbol_table.add(rm_sym);
    }
    const symbolt &rm = symbol_table.lookup_ref(rm_id);
    full_body.add(code_frontend_assignt{
      rm.symbol_expr(), from_integer(0, signedbv_typet{32})});
  }
  for(auto &stmt : start_body.statements())
    full_body.add(std::move(stmt));

  std::string start_name = "__CPROVER__start";
  irep_idt start_id{start_name};

  {
    code_typet start_type{{}, empty_typet{}};
    symbolt start_sym{start_id, start_type, "typescript"};
    start_sym.base_name = start_name;
    start_sym.is_lvalue = true;
    start_sym.value = full_body;
    symbol_table.add(start_sym);
  }
}

// --- Main entry point ---

bool typescript_convertert::convert()
{
  const jsont &statements = json_member(ast_json, "statements");
  convert_module_body(statements);
  return false; // success
}
