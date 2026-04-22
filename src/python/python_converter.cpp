/// \file
/// Python to GOTO converter — converts Python JSON AST to CBMC symbol table

#include "python_converter.h"

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/config.h>
#include <util/cprover_prefix.h>
#include <util/ieee_float.h>
#include <util/json.h>
#include <util/mathematical_types.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/symbol.h>

#include <sstream>

python_convertert::python_convertert(
  symbol_table_baset &_symbol_table,
  const python_parse_treet &_parse_tree,
  message_handlert &_message_handler)
  : symbol_table{_symbol_table},
    parse_tree{_parse_tree},
    log{_message_handler},
    filename{_parse_tree.filename}
{
}

// --- JSON helpers ---

const jsont &
python_convertert::json_member(const jsont &obj, const std::string &key) const
{
  if(obj.is_object())
  {
    const auto &object = to_json_object(obj);
    auto it = object.find(key);
    if(it != object.end())
      return it->second;
  }
  return jsont::null_json_object;
}

std::string python_convertert::json_string(const jsont &node) const
{
  if(node.is_string())
    return node.value;
  return "";
}

long long python_convertert::json_integer(const jsont &node) const
{
  if(node.is_number())
  {
    std::istringstream iss{node.value};
    long long val = 0;
    iss >> val;
    return val;
  }
  return 0;
}

bool python_convertert::is_node_type(
  const jsont &node,
  const std::string &type_name) const
{
  return json_string(json_member(node, "_type")) == type_name;
}

const json_arrayt &python_convertert::as_array(const jsont &node) const
{
  if(node.is_array())
    return to_json_array(node);
  static const json_arrayt empty;
  return empty;
}

source_locationt python_convertert::get_location(const jsont &node) const
{
  source_locationt loc;
  loc.set_file(filename);

  const jsont &lineno = json_member(node, "lineno");
  if(lineno.is_number())
    loc.set_line(lineno.value);

  const jsont &col = json_member(node, "col_offset");
  if(col.is_number())
    loc.set_column(col.value);

  return loc;
}

// --- Type conversion ---

typet python_convertert::convert_type_annotation(const jsont &annotation)
{
  if(annotation.is_null())
    return signedbv_typet{64}; // default to int

  std::string type_name = json_string(json_member(annotation, "id"));

  if(type_name == "int")
    return signedbv_typet{64};
  else if(type_name == "float")
    return double_type();
  else if(type_name == "bool")
    return bool_typet{};
  else
  {
    log.warning() << "Unknown Python type annotation: " << type_name
                  << ", defaulting to int" << messaget::eom;
    return signedbv_typet{64};
  }
}

// --- Expression conversion ---

exprt python_convertert::convert_expression(const jsont &expr)
{
  std::string node_type = json_string(json_member(expr, "_type"));

  if(node_type == "Constant")
    return convert_constant(expr);
  else if(node_type == "Name")
    return convert_name(expr);
  else if(node_type == "BinOp")
    return convert_bin_op(expr);
  else if(node_type == "UnaryOp")
    return convert_unary_op(expr);
  else if(node_type == "BoolOp")
    return convert_bool_op(expr);
  else if(node_type == "Compare")
    return convert_compare(expr);
  else if(node_type == "Call")
    return convert_call(expr);
  else if(node_type == "IfExp")
    return convert_if_exp(expr);
  else
  {
    log.error() << "Unsupported Python expression type: " << node_type
                << messaget::eom;
    return nil_exprt{};
  }
}

exprt python_convertert::convert_constant(const jsont &expr)
{
  const jsont &value = json_member(expr, "value");
  source_locationt loc = get_location(expr);

  if(value.is_true())
  {
    return true_exprt{};
  }
  else if(value.is_false())
  {
    return false_exprt{};
  }
  else if(value.is_null())
  {
    // Python None — for now treat as 0
    return from_integer(0, signedbv_typet{64});
  }
  else if(value.is_number())
  {
    std::string val_str = value.value;
    // Check if it's a float
    if(
      val_str.find('.') != std::string::npos ||
      val_str.find('e') != std::string::npos ||
      val_str.find('E') != std::string::npos)
    {
      ieee_floatt ieee_val{
        ieee_float_spect::double_precision(),
        ieee_floatt::rounding_modet::ROUND_TO_EVEN};
      ieee_val.from_double(std::stod(val_str));
      return ieee_val.to_expr();
    }
    else
    {
      mp_integer int_val{std::stoll(val_str)};
      return from_integer(int_val, signedbv_typet{64});
    }
  }
  else if(value.is_string())
  {
    // String literal — not yet supported in expressions
    log.warning() << "String literals not yet supported" << messaget::eom;
    return nil_exprt{};
  }

  log.error() << "Unsupported constant value" << messaget::eom;
  return nil_exprt{};
}

exprt python_convertert::convert_name(const jsont &expr)
{
  std::string id = json_string(json_member(expr, "id"));

  if(id == "True")
    return true_exprt{};
  else if(id == "False")
    return false_exprt{};
  else if(id == "None")
    return from_integer(0, signedbv_typet{64});

  // Look up in symbol table — try function-scoped first, then global
  const symbolt *sym = nullptr;
  if(!current_function.empty())
  {
    irep_idt scoped_id{"python::" + current_function + "::" + id};
    sym = symbol_table.lookup(scoped_id);
  }
  if(sym == nullptr)
  {
    irep_idt global_id{"python::" + id};
    sym = symbol_table.lookup(global_id);
  }
  if(sym == nullptr)
  {
    log.error() << "Unknown variable: " << id << messaget::eom;
    return nil_exprt{};
  }

  return sym->symbol_expr();
}

exprt python_convertert::convert_bin_op(const jsont &expr)
{
  exprt left = convert_expression(json_member(expr, "left"));
  exprt right = convert_expression(json_member(expr, "right"));
  std::string op = json_string(json_member(json_member(expr, "op"), "_type"));

  if(left.is_nil() || right.is_nil())
    return nil_exprt{};

  // Type promotion: if either operand is float, promote both
  if(left.type() != right.type())
  {
    if(left.type().id() == ID_floatbv)
      right = typecast_exprt{right, left.type()};
    else if(right.type().id() == ID_floatbv)
      left = typecast_exprt{left, right.type()};
  }

  if(op == "Add")
    return plus_exprt{left, right};
  else if(op == "Sub")
    return minus_exprt{left, right};
  else if(op == "Mult")
    return mult_exprt{left, right};
  else if(op == "FloorDiv")
    return div_exprt{left, right};
  else if(op == "Mod")
    return mod_exprt{left, right};
  else if(op == "Pow")
  {
    // Power not directly supported — would need a library model
    log.error() << "Power operator (**) not yet supported" << messaget::eom;
    return nil_exprt{};
  }
  else if(op == "Div")
  {
    // True division — promote to float
    typet float_type = double_type();
    return div_exprt{
      typecast_exprt{left, float_type}, typecast_exprt{right, float_type}};
  }
  else
  {
    log.error() << "Unsupported binary operator: " << op << messaget::eom;
    return nil_exprt{};
  }
}

exprt python_convertert::convert_unary_op(const jsont &expr)
{
  exprt operand = convert_expression(json_member(expr, "operand"));
  std::string op = json_string(json_member(json_member(expr, "op"), "_type"));

  if(operand.is_nil())
    return nil_exprt{};

  if(op == "USub")
    return unary_minus_exprt{operand};
  else if(op == "UAdd")
    return operand;
  else if(op == "Not")
    return not_exprt{operand};
  else
  {
    log.error() << "Unsupported unary operator: " << op << messaget::eom;
    return nil_exprt{};
  }
}

exprt python_convertert::convert_bool_op(const jsont &expr)
{
  std::string op = json_string(json_member(json_member(expr, "op"), "_type"));
  const jsont &values = json_member(expr, "values");

  if(!values.is_array() || as_array(values).size() < 2)
  {
    log.error() << "BoolOp requires at least 2 operands" << messaget::eom;
    return nil_exprt{};
  }

  exprt result = convert_expression(*as_array(values).begin());
  auto it = std::next(as_array(values).begin());
  for(; it != as_array(values).end(); ++it)
  {
    exprt next = convert_expression(*it);
    if(result.is_nil() || next.is_nil())
      return nil_exprt{};

    if(op == "And")
      result = and_exprt{result, next};
    else if(op == "Or")
      result = or_exprt{result, next};
    else
    {
      log.error() << "Unsupported bool operator: " << op << messaget::eom;
      return nil_exprt{};
    }
  }

  return result;
}

exprt python_convertert::convert_compare(const jsont &expr)
{
  exprt left = convert_expression(json_member(expr, "left"));
  const jsont &ops = json_member(expr, "ops");
  const jsont &comparators = json_member(expr, "comparators");

  if(
    !ops.is_array() || !comparators.is_array() || as_array(ops).empty() ||
    as_array(comparators).empty())
  {
    log.error() << "Malformed Compare node" << messaget::eom;
    return nil_exprt{};
  }

  // Handle chained comparisons: a < b < c → (a < b) and (b < c)
  exprt result = nil_exprt{};
  exprt current_left = left;

  auto ops_it = as_array(ops).begin();
  auto comp_it = as_array(comparators).begin();
  for(; ops_it != as_array(ops).end(); ++ops_it, ++comp_it)
  {
    std::string op = json_string(json_member(*ops_it, "_type"));
    exprt right = convert_expression(*comp_it);

    if(current_left.is_nil() || right.is_nil())
      return nil_exprt{};

    // Type promotion for comparisons
    if(current_left.type() != right.type())
    {
      if(current_left.type().id() == ID_floatbv)
        right = typecast_exprt{right, current_left.type()};
      else if(right.type().id() == ID_floatbv)
        current_left = typecast_exprt{current_left, right.type()};
    }

    exprt cmp;
    if(op == "Eq")
      cmp = equal_exprt{current_left, right};
    else if(op == "NotEq")
      cmp = notequal_exprt{current_left, right};
    else if(op == "Lt")
      cmp = binary_relation_exprt{current_left, ID_lt, right};
    else if(op == "LtE")
      cmp = binary_relation_exprt{current_left, ID_le, right};
    else if(op == "Gt")
      cmp = binary_relation_exprt{current_left, ID_gt, right};
    else if(op == "GtE")
      cmp = binary_relation_exprt{current_left, ID_ge, right};
    else
    {
      log.error() << "Unsupported comparison operator: " << op << messaget::eom;
      return nil_exprt{};
    }

    if(result.is_nil())
      result = cmp;
    else
      result = and_exprt{result, cmp};

    current_left = right;
  }

  return result;
}

exprt python_convertert::convert_call(const jsont &expr)
{
  const jsont &func = json_member(expr, "func");
  const jsont &args = json_member(expr, "args");

  std::string func_name;
  if(is_node_type(func, "Name"))
    func_name = json_string(json_member(func, "id"));

  // Handle nondet functions
  if(func_name == "nondet_int")
  {
    side_effect_expr_nondett nondet{signedbv_typet{64}, get_location(expr)};
    return std::move(nondet);
  }
  else if(func_name == "nondet_float")
  {
    side_effect_expr_nondett nondet{double_type(), get_location(expr)};
    return std::move(nondet);
  }
  else if(func_name == "nondet_bool")
  {
    side_effect_expr_nondett nondet{bool_typet{}, get_location(expr)};
    return std::move(nondet);
  }

  // Regular function call
  irep_idt symbol_id{"python::" + func_name};
  const symbolt *sym = symbol_table.lookup(symbol_id);
  if(sym == nullptr)
  {
    log.error() << "Unknown function: " << func_name << messaget::eom;
    return nil_exprt{};
  }

  const code_typet &func_type = to_code_type(sym->type);

  exprt::operandst arguments;
  if(args.is_array())
  {
    for(const auto &arg : as_array(args))
      arguments.push_back(convert_expression(arg));
  }

  side_effect_expr_function_callt call{
    sym->symbol_expr(),
    std::move(arguments),
    func_type.return_type(),
    get_location(expr)};

  return std::move(call);
}

exprt python_convertert::convert_if_exp(const jsont &expr)
{
  exprt test = convert_expression(json_member(expr, "test"));
  exprt body = convert_expression(json_member(expr, "body"));
  exprt orelse = convert_expression(json_member(expr, "orelse"));

  if(test.is_nil() || body.is_nil() || orelse.is_nil())
    return nil_exprt{};

  return if_exprt{test, body, orelse};
}

// --- Statement conversion ---

codet python_convertert::convert_statement(const jsont &stmt)
{
  std::string node_type = json_string(json_member(stmt, "_type"));

  if(node_type == "AnnAssign")
    return convert_ann_assign(stmt);
  else if(node_type == "Assign")
    return convert_assign(stmt);
  else if(node_type == "AugAssign")
    return convert_aug_assign(stmt);
  else if(node_type == "Assert")
    return convert_assert(stmt);
  else if(node_type == "If")
    return convert_if(stmt);
  else if(node_type == "While")
    return convert_while(stmt);
  else if(node_type == "For")
    return convert_for(stmt);
  else if(node_type == "Return")
    return convert_return(stmt);
  else if(node_type == "FunctionDef")
    return convert_function_def(stmt);
  else if(node_type == "Expr")
    return convert_expr_stmt(stmt);
  else if(node_type == "Break")
    return convert_break();
  else if(node_type == "Continue")
    return convert_continue();
  else if(node_type == "Pass")
    return convert_pass();
  else
  {
    log.warning() << "Unsupported Python statement type: " << node_type
                  << messaget::eom;
    return code_skipt{};
  }
}

codet python_convertert::convert_ann_assign(const jsont &stmt)
{
  // x: int = 5
  const jsont &target = json_member(stmt, "target");
  const jsont &annotation = json_member(stmt, "annotation");
  const jsont &value = json_member(stmt, "value");

  std::string var_name = json_string(json_member(target, "id"));
  typet var_type = convert_type_annotation(annotation);
  source_locationt loc = get_location(stmt);

  std::string qualified_name =
    current_function.empty() ? "python::" + var_name
                             : "python::" + current_function + "::" + var_name;
  irep_idt symbol_id{qualified_name};

  // Create symbol if it doesn't exist
  if(symbol_table.lookup(symbol_id) == nullptr)
  {
    symbolt new_symbol{symbol_id, var_type, "python"};
    new_symbol.base_name = var_name;
    new_symbol.location = loc;
    new_symbol.is_lvalue = true;
    new_symbol.is_state_var = true;
    new_symbol.is_static_lifetime = current_function.empty();
    symbol_table.add(new_symbol);
  }

  if(value.is_null())
    return code_skipt{};

  exprt rhs = convert_expression(value);
  if(rhs.is_nil())
    return code_skipt{};

  const symbolt &sym = symbol_table.lookup_ref(symbol_id);

  // Type cast if needed
  if(rhs.type() != sym.type)
    rhs = typecast_exprt{rhs, sym.type};

  code_frontend_assignt assign{sym.symbol_expr(), rhs};
  assign.add_source_location() = loc;
  return std::move(assign);
}

codet python_convertert::convert_assign(const jsont &stmt)
{
  // x = expr  OR  a = b = expr (multiple targets)
  const jsont &targets = json_member(stmt, "targets");
  const jsont &value = json_member(stmt, "value");

  if(!targets.is_array() || as_array(targets).empty())
    return code_skipt{};

  source_locationt loc = get_location(stmt);
  exprt rhs = convert_expression(value);
  if(rhs.is_nil())
    return code_skipt{};

  code_blockt block;

  for(const auto &target : as_array(targets))
  {
    std::string var_name = json_string(json_member(target, "id"));
    std::string qualified_name =
      current_function.empty()
        ? "python::" + var_name
        : "python::" + current_function + "::" + var_name;
    irep_idt symbol_id{qualified_name};

    if(symbol_table.lookup(symbol_id) == nullptr)
    {
      symbolt new_symbol{symbol_id, rhs.type(), "python"};
      new_symbol.base_name = var_name;
      new_symbol.location = loc;
      new_symbol.is_lvalue = true;
      new_symbol.is_state_var = true;
      new_symbol.is_static_lifetime = current_function.empty();
      symbol_table.add(new_symbol);
    }

    const symbolt &sym = symbol_table.lookup_ref(symbol_id);
    exprt typed_rhs = rhs;
    if(typed_rhs.type() != sym.type)
      typed_rhs = typecast_exprt{typed_rhs, sym.type};

    code_frontend_assignt assign{sym.symbol_expr(), typed_rhs};
    assign.add_source_location() = loc;
    block.add(std::move(assign));
  }

  if(block.statements().size() == 1)
    return block.statements().front();
  return std::move(block);
}

codet python_convertert::convert_aug_assign(const jsont &stmt)
{
  // x += expr  →  x = x + expr
  const jsont &target = json_member(stmt, "target");
  const jsont &op_node = json_member(stmt, "op");
  const jsont &value = json_member(stmt, "value");

  std::string var_name = json_string(json_member(target, "id"));
  source_locationt loc = get_location(stmt);

  exprt lhs = convert_name(target);
  exprt rhs = convert_expression(value);
  if(lhs.is_nil() || rhs.is_nil())
    return code_skipt{};

  std::string op = json_string(json_member(op_node, "_type"));

  // Type promotion
  if(lhs.type() != rhs.type())
  {
    if(lhs.type().id() == ID_floatbv)
      rhs = typecast_exprt{rhs, lhs.type()};
    else if(rhs.type().id() == ID_floatbv)
      rhs = typecast_exprt{rhs, lhs.type()};
  }

  exprt new_rhs;
  if(op == "Add")
    new_rhs = plus_exprt{lhs, rhs};
  else if(op == "Sub")
    new_rhs = minus_exprt{lhs, rhs};
  else if(op == "Mult")
    new_rhs = mult_exprt{lhs, rhs};
  else if(op == "FloorDiv")
    new_rhs = div_exprt{lhs, rhs};
  else if(op == "Mod")
    new_rhs = mod_exprt{lhs, rhs};
  else
  {
    log.error() << "Unsupported augmented assignment operator: " << op
                << messaget::eom;
    return code_skipt{};
  }

  code_frontend_assignt assign{lhs, new_rhs};
  assign.add_source_location() = loc;
  return std::move(assign);
}

codet python_convertert::convert_assert(const jsont &stmt)
{
  exprt test = convert_expression(json_member(stmt, "test"));
  if(test.is_nil())
    return code_skipt{};

  // Ensure the test is boolean
  if(test.type() != bool_typet{})
    test = typecast_exprt{test, bool_typet{}};

  source_locationt loc = get_location(stmt);
  loc.set_property_class("assertion");
  loc.set_comment("Python assertion");

  code_assertt assertion{test};
  assertion.add_source_location() = loc;
  return std::move(assertion);
}

codet python_convertert::convert_if(const jsont &stmt)
{
  exprt test = convert_expression(json_member(stmt, "test"));
  if(test.is_nil())
    return code_skipt{};

  if(test.type() != bool_typet{})
    test = typecast_exprt{test, bool_typet{}};

  // Convert body
  code_blockt then_block;
  const jsont &body = json_member(stmt, "body");
  if(body.is_array())
  {
    for(const auto &s : as_array(body))
      then_block.add(convert_statement(s));
  }

  // Convert orelse (may be empty, elif chain, or else block)
  const jsont &orelse = json_member(stmt, "orelse");
  if(orelse.is_array() && !as_array(orelse).empty())
  {
    code_blockt else_block;
    for(const auto &s : as_array(orelse))
      else_block.add(convert_statement(s));

    code_ifthenelset if_stmt{
      test, std::move(then_block), std::move(else_block)};
    if_stmt.add_source_location() = get_location(stmt);
    return std::move(if_stmt);
  }
  else
  {
    code_ifthenelset if_stmt{test, std::move(then_block)};
    if_stmt.add_source_location() = get_location(stmt);
    return std::move(if_stmt);
  }
}

codet python_convertert::convert_while(const jsont &stmt)
{
  exprt test = convert_expression(json_member(stmt, "test"));
  if(test.is_nil())
    return code_skipt{};

  if(test.type() != bool_typet{})
    test = typecast_exprt{test, bool_typet{}};

  code_blockt body_block;
  const jsont &body = json_member(stmt, "body");
  if(body.is_array())
  {
    for(const auto &s : as_array(body))
      body_block.add(convert_statement(s));
  }

  code_whilet while_stmt{test, std::move(body_block)};
  while_stmt.add_source_location() = get_location(stmt);
  return std::move(while_stmt);
}

codet python_convertert::convert_for(const jsont &stmt)
{
  // for i in range(n): body
  // Desugar to: i = 0; while(i < n) { body; i = i + 1; }
  const jsont &target = json_member(stmt, "target");
  const jsont &iter = json_member(stmt, "iter");
  source_locationt loc = get_location(stmt);

  // Only support range() for now
  if(!is_node_type(iter, "Call"))
  {
    log.error() << "Only 'for x in range(...)' is supported" << messaget::eom;
    return code_skipt{};
  }

  std::string func_name =
    json_string(json_member(json_member(iter, "func"), "id"));
  if(func_name != "range")
  {
    log.error() << "Only 'for x in range(...)' is supported" << messaget::eom;
    return code_skipt{};
  }

  const jsont &range_args = json_member(iter, "args");
  if(!range_args.is_array() || as_array(range_args).empty())
    return code_skipt{};

  // range(stop) or range(start, stop) or range(start, stop, step)
  exprt start, stop;
  typet int_type = signedbv_typet{64};

  if(as_array(range_args).size() == 1)
  {
    start = from_integer(0, int_type);
    stop = convert_expression(*std::next(as_array(range_args).begin(), 0));
  }
  else
  {
    start = convert_expression(*std::next(as_array(range_args).begin(), 0));
    stop = convert_expression(*std::next(as_array(range_args).begin(), 1));
  }

  // Create loop variable
  std::string var_name = json_string(json_member(target, "id"));
  std::string qualified_name =
    current_function.empty() ? "python::" + var_name
                             : "python::" + current_function + "::" + var_name;
  irep_idt symbol_id{qualified_name};

  if(symbol_table.lookup(symbol_id) == nullptr)
  {
    symbolt new_symbol{symbol_id, int_type, "python"};
    new_symbol.base_name = var_name;
    new_symbol.location = loc;
    new_symbol.is_lvalue = true;
    new_symbol.is_state_var = true;
    new_symbol.is_static_lifetime = false;
    symbol_table.add(new_symbol);
  }

  const symbolt &loop_var = symbol_table.lookup_ref(symbol_id);
  symbol_exprt loop_sym = loop_var.symbol_expr();

  // i = start
  code_frontend_assignt init{loop_sym, start};
  init.add_source_location() = loc;

  // while(i < stop)
  binary_relation_exprt cond{loop_sym, ID_lt, stop};

  // body + i = i + 1
  code_blockt body_block;
  const jsont &body = json_member(stmt, "body");
  if(body.is_array())
  {
    for(const auto &s : as_array(body))
      body_block.add(convert_statement(s));
  }

  code_frontend_assignt increment{
    loop_sym, plus_exprt{loop_sym, from_integer(1, int_type)}};
  increment.add_source_location() = loc;
  body_block.add(std::move(increment));

  code_whilet while_stmt{cond, std::move(body_block)};
  while_stmt.add_source_location() = loc;

  code_blockt result;
  result.add(std::move(init));
  result.add(std::move(while_stmt));
  return std::move(result);
}

codet python_convertert::convert_return(const jsont &stmt)
{
  const jsont &value = json_member(stmt, "value");

  if(value.is_null())
    return code_frontend_returnt{};

  exprt ret_val = convert_expression(value);
  if(ret_val.is_nil())
    return code_frontend_returnt{};

  code_frontend_returnt ret{ret_val};
  ret.add_source_location() = get_location(stmt);
  return std::move(ret);
}

codet python_convertert::convert_function_def(const jsont &stmt)
{
  std::string func_name = json_string(json_member(stmt, "name"));
  source_locationt loc = get_location(stmt);

  // Build parameter list
  const jsont &args_node = json_member(stmt, "args");
  const jsont &params = json_member(args_node, "args");

  code_typet::parameterst parameters;
  if(params.is_array())
  {
    for(const auto &param : as_array(params))
    {
      std::string param_name = json_string(json_member(param, "arg"));
      const jsont &annotation = json_member(param, "annotation");
      typet param_type = convert_type_annotation(annotation);

      code_typet::parametert p{param_type};
      p.set_identifier("python::" + func_name + "::" + param_name);
      p.set_base_name(param_name);
      parameters.push_back(p);
    }
  }

  // Return type
  const jsont &returns = json_member(stmt, "returns");
  typet return_type =
    returns.is_null() ? empty_typet{} : convert_type_annotation(returns);

  code_typet func_type{parameters, return_type};

  // Create function symbol BEFORE converting the body
  // (so recursive calls can find it)
  irep_idt symbol_id{"python::" + func_name};

  if(symbol_table.lookup(symbol_id) == nullptr)
  {
    symbolt func_symbol{symbol_id, func_type, "python"};
    func_symbol.base_name = func_name;
    func_symbol.location = loc;
    func_symbol.is_lvalue = true;
    symbol_table.add(func_symbol);
  }

  // Create parameter symbols
  for(const auto &p : parameters)
  {
    symbolt param_symbol{p.get_identifier(), p.type(), "python"};
    param_symbol.base_name = p.get_base_name();
    param_symbol.location = loc;
    param_symbol.is_lvalue = true;
    param_symbol.is_state_var = true;
    param_symbol.is_parameter = true;
    if(symbol_table.lookup(param_symbol.name) == nullptr)
      symbol_table.add(param_symbol);
  }

  // Convert function body
  std::string saved_function = current_function;
  current_function = func_name;

  code_blockt body_block;
  const jsont &body = json_member(stmt, "body");
  if(body.is_array())
  {
    for(const auto &s : as_array(body))
      body_block.add(convert_statement(s));
  }

  current_function = saved_function;

  // Update the symbol with the body
  symbolt *sym_ptr = symbol_table.get_writeable(symbol_id);
  if(sym_ptr != nullptr)
    sym_ptr->value = body_block;

  // Function definitions don't produce executable code at the call site
  return code_skipt{};
}

codet python_convertert::convert_expr_stmt(const jsont &stmt)
{
  // Expression statement (e.g., function call as statement)
  const jsont &value = json_member(stmt, "value");

  // Check for __CPROVER_assume calls
  if(is_node_type(value, "Call"))
  {
    const jsont &func = json_member(value, "func");
    std::string func_name;
    if(is_node_type(func, "Name"))
      func_name = json_string(json_member(func, "id"));

    if(func_name == "__CPROVER_assume")
    {
      const jsont &args = json_member(value, "args");
      if(args.is_array() && !as_array(args).empty())
      {
        exprt cond = convert_expression(*as_array(args).begin());
        if(cond.type() != bool_typet{})
          cond = typecast_exprt{cond, bool_typet{}};
        code_assumet assume{cond};
        assume.add_source_location() = get_location(stmt);
        return std::move(assume);
      }
    }
  }

  exprt expr = convert_expression(value);

  if(expr.is_nil())
    return code_skipt{};

  code_expressiont code_expr{expr};
  code_expr.add_source_location() = get_location(stmt);
  return std::move(code_expr);
}

codet python_convertert::convert_break()
{
  return code_breakt{};
}

codet python_convertert::convert_continue()
{
  return code_continuet{};
}

codet python_convertert::convert_pass()
{
  return code_skipt{};
}

// --- Module body conversion ---

code_blockt python_convertert::convert_module_body(const jsont &body)
{
  code_blockt block;

  if(!body.is_array())
    return block;

  for(const auto &stmt : as_array(body))
  {
    // Function definitions are handled in the first pass
    if(is_node_type(stmt, "FunctionDef"))
      continue;

    codet code = convert_statement(stmt);
    block.add(std::move(code));
  }

  return block;
}

// --- Main entry point ---

bool python_convertert::convert()
{
  const jsont &body = json_member(parse_tree.ast_json, "body");

  // First pass: register all function definitions (signatures only)
  if(body.is_array())
  {
    for(const auto &stmt : as_array(body))
    {
      if(is_node_type(stmt, "FunctionDef"))
        convert_function_def(stmt);
    }
  }

  // Second pass: convert top-level statements (excluding function defs)
  code_blockt module_body = convert_module_body(body);

  // Create __CPROVER__start function (note: double underscore before "start")
  code_typet start_type{{}, empty_typet{}};
  const std::string start_name = std::string{CPROVER_PREFIX} + "_start";
  irep_idt start_id{start_name};

  symbolt start_symbol{start_id, start_type, "python"};
  start_symbol.base_name = start_name;
  start_symbol.is_lvalue = true;
  start_symbol.value = module_body;

  if(symbol_table.lookup(start_id) != nullptr)
    symbol_table.remove(start_id);

  symbol_table.add(start_symbol);

  // Create __CPROVER_initialize (empty for now)
  const std::string init_name = std::string{CPROVER_PREFIX} + "initialize";
  irep_idt init_id{init_name};
  if(symbol_table.lookup(init_id) == nullptr)
  {
    symbolt init_symbol{init_id, code_typet{{}, empty_typet{}}, "python"};
    init_symbol.base_name = init_name;
    init_symbol.is_lvalue = true;
    init_symbol.value = code_blockt{};
    symbol_table.add(init_symbol);
  }

  // Create __CPROVER_rounding_mode (needed for float operations)
  irep_idt rounding_id{CPROVER_PREFIX "rounding_mode"};
  if(symbol_table.lookup(rounding_id) == nullptr)
  {
    symbolt rounding_symbol{rounding_id, signed_int_type(), "python"};
    rounding_symbol.base_name = CPROVER_PREFIX "rounding_mode";
    rounding_symbol.is_thread_local = true;
    rounding_symbol.is_static_lifetime = true;
    rounding_symbol.value = from_integer(
      static_cast<int>(ieee_floatt::rounding_modet::ROUND_TO_EVEN),
      signed_int_type());
    symbol_table.add(rounding_symbol);
  }

  return false;
}
