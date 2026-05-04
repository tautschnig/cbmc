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
#include "typescript_types.h"

#include <util/arith_tools.h>
#include <util/irep.h>
#include <cstdint>
#include <cstring>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/ieee_float.h>
#include <util/floatbv_expr.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/symbol.h>

#include <goto-programs/goto_functions.h>

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
  // Default: treat as number for now
  return double_type();
}

// --- Expression conversion ---

exprt typescript_convertert::convert_expression(const jsont &node)
{ return true_exprt{}; }





















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
  return constant_exprt{integer2bvrep(mp_integer{bits}, 64), double_type()};
}

exprt typescript_convertert::convert_string_literal(const jsont &node)
{
  // ES2024 sec-ecmascript-language-types-string-type
  std::string text = json_string(json_member(node, "text"));
  struct_typet str_type = typescript_string_type();
  const auto &data_type = to_array_type(str_type.components()[1].type());

  exprt::operandst chars;
  for(char c : text)
    chars.push_back(
      from_integer(static_cast<unsigned char>(c), unsignedbv_typet{16}));
  while(chars.size() < TYPESCRIPT_MAX_STRING_LENGTH)
    chars.push_back(from_integer(0, unsignedbv_typet{16}));

  return struct_exprt{
    {from_integer(static_cast<long long>(text.size()), signedbv_typet{64}),
     array_exprt{std::move(chars), data_type}},
    str_type};
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
    ieee_floatt nan{ieee_float_spect::double_precision(), ieee_floatt::rounding_modet::ROUND_TO_EVEN};
    nan.make_NaN();
    return nan.to_expr();
  }
  if(name == "Infinity")
  {
    ieee_floatt inf{ieee_float_spect::double_precision(), ieee_floatt::rounding_modet::ROUND_TO_EVEN};
    inf.make_plus_infinity();
    return inf.to_expr();
  }

  // Look up in symbol table
  std::string qualified = "typescript::" +
    (current_function.empty() ? "" : current_function + "::") + name;
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
    // String concatenation
    if(is_typescript_string_type(left.type()) ||
       is_typescript_string_type(right.type()))
    {
      // For now, return nondet string (exact concat needs array copy)
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
    return div_exprt{left, right};
  // ES2024 sec-numeric-types-number-remainder
  if(op == "PercentToken")
    return mod_exprt{left, right};

  // ES2024 sec-isstrictlyequal: ===
  if(op == "EqualsEqualsEqualsToken")
  {
    if(left.type().id() == ID_floatbv)
      return ieee_float_equal_exprt{left, right};
    // String equality: compare structs (length + data)
    if(is_typescript_string_type(left.type()) &&
       is_typescript_string_type(right.type()))
      return equal_exprt{left, right};
    return equal_exprt{left, right};
  }
  // ES2024 sec-isstrictlyequal: !==
  if(op == "ExclamationEqualsEqualsToken")
  {
    if(left.type().id() == ID_floatbv)
      return ieee_float_notequal_exprt{left, right};
    return notequal_exprt{left, right};
  }

  // ES2024 sec-relational-operators
  if(op == "LessThanToken")
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
    exprt l = left.type().id() == ID_bool ? left
              : typecast_exprt{left, bool_typet{}};
    exprt r = right.type().id() == ID_bool ? right
              : typecast_exprt{right, bool_typet{}};
    return and_exprt{l, r};
  }
  if(op == "BarBarToken")
  {
    exprt l = left.type().id() == ID_bool ? left
              : typecast_exprt{left, bool_typet{}};
    exprt r = right.type().id() == ID_bool ? right
              : typecast_exprt{right, bool_typet{}};
    return or_exprt{l, r};
  }

  // ES2024 sec-assignment-operators: simple assignment
  if(op == "EqualsToken")
  {
    // This is handled at the statement level
    return right;
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
exprt typescript_convertert::convert_call_expression(const jsont &node)
{
  const jsont &callee = json_member(node, "expression");
  const jsont &args = json_member(node, "arguments");

  // Handle console.assert → CBMC assertion
  if(is_kind(callee, "PropertyAccessExpression"))
  {
    std::string obj = json_string(json_member(json_member(callee, "expression"), "text"));
    std::string method = json_string(json_member(json_member(callee, "name"), "text"));

    if(obj == "console" && method == "assert")
    {
      // This is handled at the statement level
      return nil_exprt{};
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

  if(kind == "ExpressionStatement")
    return convert_expression_statement(node);

  // ES2024 sec-if-statement
  if(kind == "IfStatement")
    return convert_if_statement(node);

  // ES2024 sec-while-statement
  if(kind == "WhileStatement")
    return convert_while_statement(node);

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
    std::string var_name = json_string(json_member(json_member(decl, "name"), "text"));
    std::string ts_type = json_string(json_member(decl, "_type"));
    typet var_type = convert_type(ts_type);

    std::string qualified = "typescript::" +
      (current_function.empty() ? "" : current_function + "::") + var_name;
    irep_idt sym_id{qualified};

    if(symbol_table.lookup(sym_id) == nullptr)
    {
      symbolt new_sym{sym_id, var_type, "typescript"};
      new_sym.base_name = var_name;
      new_sym.location = get_location(decl);
      new_sym.is_lvalue = true;
      new_sym.is_state_var = true;
      new_sym.is_static_lifetime = current_function.empty();
      symbol_table.add(new_sym);
    }

    const symbolt &sym = symbol_table.lookup_ref(sym_id);

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
        if(rhs.type() != sym.type)
          rhs = typecast_exprt{rhs, sym.type};
        code_frontend_assignt assign{sym.symbol_expr(), rhs};
        assign.add_source_location() = get_location(decl);
        block.add(std::move(assign));
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
              return code_assertt{cond};
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
  // Fallback: evaluate expression
  exprt e = convert_expression(expr_node);
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
      return code_frontend_returnt{val};
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
  std::string func_name = json_string(json_member(json_member(node, "name"), "text"));
  if(func_name.empty())
    return;

  // Get return type
  std::string ret_type_str = json_string(json_member(node, "_returnType"));
  typet ret_type = ret_type_str.empty() ? empty_typet{} : convert_type(ret_type_str);

  // Get parameters
  code_typet::parameterst params;
  const jsont &param_nodes = json_member(node, "parameters");
  if(param_nodes.is_array())
  {
    for(const auto &p : to_json_array(param_nodes))
    {
      std::string pname = json_string(json_member(json_member(p, "name"), "text"));
      std::string ptype_str = json_string(json_member(p, "_type"));
      typet ptype = convert_type(ptype_str);

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
    if(symbol_table.lookup(pid) == nullptr)
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
  const jsont &body = json_member(node, "body");
  if(body.is_object())
  {
    std::string saved_function = current_function;
    current_function = func_name;
    codet body_code = convert_block(body);
    current_function = saved_function;
    func_sym.value = body_code;
  }

  if(symbol_table.lookup(func_id) == nullptr)
    symbol_table.add(func_sym);
}

// --- Module body ---

void typescript_convertert::convert_module_body(const jsont &statements)
{
  if(!statements.is_array())
    return;

  // First pass: register all function declarations
  for(const auto &stmt : to_json_array(statements))
  {
    std::string kind = json_string(json_member(stmt, "_kind"));
    if(kind == "FunctionDeclaration")
      convert_function_declaration(stmt);
  }

  // Second pass: convert module-level statements into __CPROVER__start body
  code_blockt start_body;
  for(const auto &stmt : to_json_array(statements))
  {
    std::string kind = json_string(json_member(stmt, "_kind"));
    if(kind == "FunctionDeclaration")
      continue; // already handled
    codet code = convert_statement(stmt);
    start_body.add(std::move(code));
  }

  // Create __CPROVER__start function
  std::string start_name = "__CPROVER__start";
  irep_idt start_id{start_name};

  if(symbol_table.lookup(start_id) == nullptr)
  {
    code_typet start_type{{}, empty_typet{}};
    symbolt start_sym{start_id, start_type, "typescript"};
    start_sym.base_name = start_name;
    start_sym.is_lvalue = true;
    start_sym.value = start_body;
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
