/*******************************************************************\

Module: Go front-end: conversion of the Go AST to CBMC symbols

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// Conversion of the in-memory Go AST into symbols with `codet` bodies.

#include "go_convert.h"

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/c_types.h>
#include <util/expr_initializer.h>
#include <util/expr_util.h>
#include <util/mp_arith.h>
#include <util/pointer_expr.h>
#include <util/std_code.h>
#include <util/symbol_table_base.h>

#include "go_parse_tree.h"

#include <set>

go_convertert::go_convertert(
  symbol_table_baset &symbol_table,
  const go_parse_treet &parse_tree,
  message_handlert &message_handler)
  : symbol_table(symbol_table),
    parse_tree(parse_tree),
    log(message_handler),
    ns(symbol_table),
    type_converter(parse_tree)
{
}

/// Id of the expression standing for the untyped nil until its context
/// determines the type (see go_convertert::convert_expr).
static const irep_idt ID_go_nil = "go_nil";

/// An expression statement evaluating \p value, at \p location.
static code_expressiont
expression_statement(exprt value, const source_locationt &location)
{
  code_expressiont result{std::move(value)};
  result.add_source_location() = location;
  return result;
}

/// \p value converted to \p type, as for an assignment (Go1.27
/// §Assignability): go/types has checked that this is valid. Integer
/// representations change as for conversions; the untyped nil becomes the
/// null pointer of \p type.
static exprt go_cast(exprt value, const typet &type)
{
  if(value.id() == ID_go_nil)
  {
    PRECONDITION(type.id() == ID_pointer);
    source_locationt location = value.source_location();
    exprt result = null_pointer_exprt{to_pointer_type(type)};
    result.add_source_location() = std::move(location);
    return result;
  }
  return typecast_exprt::conditional_cast(std::move(value), type);
}

source_locationt go_convertert::location_of(const irept &node) const
{
  // Property and loop ids are derived from the function recorded in the
  // source location (CBMC convention: the function's symbol id).
  source_locationt location = go_location(node);
  if(!state.function_id.empty() && location.is_not_nil())
    location.set_function(state.function_id);
  return location;
}

void go_convertert::error(const irept &node, const std::string &message)
{
  log.error().source_location = go_location(node);
  log.error() << message << messaget::eom;
  error_found = true;
}

void go_convertert::unsupported(const irept &node, const std::string &what)
{
  error(node, "Go front-end: " + what + " is not supported yet");
}

typet go_convertert::convert_type(const irep_idt &index, const irept &where)
{
  typet type = type_converter(index);
  if(type.is_nil())
    unsupported(where, type_converter.unsupported_reason);
  return type;
}

typet go_convertert::expr_type(const irept &expr)
{
  return convert_type(expr.get("_t"), expr);
}

void go_convertert::add_symbol(const symbolt &symbol)
{
  // Distinct Go objects must never share a symbol: a clash would silently
  // merge them (for example, two init functions).
  if(symbol_table.add(symbol))
  {
    log.error().source_location = symbol.location;
    log.error() << "Go front-end: duplicate symbol " << symbol.name
                << messaget::eom;
    error_found = true;
  }
}

static symbolt make_symbol(
  const irep_idt &id,
  const irep_idt &base_name,
  const typet &type,
  const source_locationt &location,
  const std::string &package)
{
  symbolt symbol{id, type, "go"};
  symbol.base_name = base_name;
  symbol.pretty_name = base_name;
  symbol.location = location;
  symbol.module = package;
  return symbol;
}

// ---------------------------------------------------------------- symbols

void go_convertert::declare_package_var(
  const irep_idt &object_id,
  const irept &where)
{
  const irept &object = parse_tree.object(object_id);
  typet type = convert_type(object.get("type"), where);
  if(type.is_nil())
    return;
  symbolt symbol = make_symbol(
    symbol_id(object_id),
    object.get("name"),
    type,
    location_of(where),
    parse_tree.package);
  symbol.is_static_lifetime = true;
  symbol.is_lvalue = true;
  // Go1.27 §Variable_declarations, §The_zero_value: variables without an
  // initializer start as the zero value. Initializers run later, in
  // package initialisation order (convert_package_init).
  auto zero = zero_initializer(type, symbol.location, ns);
  CHECK_RETURN(zero.has_value());
  symbol.value = *zero;
  add_symbol(symbol);
}

void go_convertert::declare_parameters(
  code_typet &code_type,
  const irept &signature,
  const irep_idt &function,
  const source_locationt &location)
{
  // Go1.27 §Function_declarations: parameters are local variables of the
  // function. Unnamed parameters still need a symbol to bind the argument.
  const auto &params = go_list(signature, "params");
  std::size_t index = 0;
  for(auto &parameter : code_type.parameters())
  {
    const irept &param = params[index];
    irep_idt param_object = param.get("obj");
    irep_idt base = param.get("name");
    if(param_object.empty() || base == "_")
    {
      param_object = id2string(function) + "::$param" + std::to_string(index);
      base = "$param" + std::to_string(index);
    }
    const irep_idt param_id = symbol_id(param_object);
    parameter.set_identifier(param_id);
    parameter.set_base_name(base);
    symbolt param_symbol = make_symbol(
      param_id, base, parameter.type(), location, parse_tree.package);
    param_symbol.is_parameter = true;
    param_symbol.is_lvalue = true;
    param_symbol.is_thread_local = true;
    param_symbol.is_file_local = true;
    add_symbol(param_symbol);
    ++index;
  }
}

void go_convertert::declare_function(const irept &decl)
{
  if(go_field(decl, "Recv").is_not_nil())
    return unsupported(decl, "methods");
  if(go_field(decl, "Body").is_nil())
    return unsupported(decl, "functions without a body");
  const irept &name = go_field(decl, "Name");
  const irep_idt &object_id = name.get("_obj");
  const irept &object = parse_tree.object(object_id);
  if(go_field(go_field(decl, "Type"), "TypeParams").is_not_nil())
    return unsupported(decl, "generic functions");

  typet type = type_converter.signature(object.get("type"));
  if(type.is_nil())
    return unsupported(decl, type_converter.unsupported_reason);
  declare_parameters(
    to_code_type(type),
    parse_tree.type_entry(object.get("type")),
    object_id,
    location_of(decl));

  symbolt symbol = make_symbol(
    symbol_id(object_id),
    name.get("Name"),
    type,
    location_of(decl),
    parse_tree.package);
  add_symbol(symbol);
}

symbol_exprt go_convertert::declare_local(
  const irep_idt &object_id,
  const typet &type,
  const source_locationt &location,
  code_blockt &dest)
{
  const irept &object = parse_tree.object(object_id);
  symbolt symbol = make_symbol(
    symbol_id(object_id),
    object.get("name"),
    type,
    location,
    parse_tree.package);
  symbol.is_lvalue = true;
  symbol.is_thread_local = true;
  symbol.is_file_local = true;
  add_symbol(symbol);
  symbol_exprt result = symbol.symbol_expr();
  code_frontend_declt decl{result};
  decl.add_source_location() = location;
  dest.add(std::move(decl));
  return result;
}

symbol_exprt go_convertert::new_temporary(
  const typet &type,
  const source_locationt &location,
  code_blockt &dest)
{
  const irep_idt base = "$tmp" + std::to_string(state.tmp_counter++);
  symbolt symbol = make_symbol(
    id2string(state.function_id) + "::" + id2string(base),
    base,
    type,
    location,
    parse_tree.package);
  symbol.is_lvalue = true;
  symbol.is_thread_local = true;
  symbol.is_file_local = true;
  add_symbol(symbol);
  symbol_exprt result = symbol.symbol_expr();
  code_frontend_declt decl{result};
  decl.add_source_location() = location;
  dest.add(std::move(decl));
  return result;
}

exprt go_convertert::variable(const irept &ident)
{
  const irep_idt &object_id = ident.get("_obj");
  const irept &object = parse_tree.object(object_id);
  const irep_idt &kind = object.get("kind");
  if(kind == "func")
  {
    // Go1.27 §Function_types: a function value referring to a declared
    // function.
    const symbolt *function = symbol_table.lookup(symbol_id(object_id));
    if(function == nullptr)
    {
      if(object.get("pkg") != parse_tree.package)
        unsupported(
          ident, "function values from package " + object.get_string("pkg"));
      return nil_exprt{};
    }
    address_of_exprt result{function->symbol_expr()};
    result.add_source_location() = location_of(ident);
    return std::move(result);
  }
  if(kind != "var")
  {
    unsupported(
      ident,
      "use of " + id2string(kind) + " '" + ident.get_string("Name") +
        "' as a value");
    return nil_exprt{};
  }
  const symbolt *symbol = symbol_table.lookup(symbol_id(object_id));
  if(symbol == nullptr)
  {
    // A variable of this package whose declaration failed has already been
    // reported; only report variables of other packages.
    if(object.get("pkg") != parse_tree.package || !error_found)
      unsupported(
        ident,
        "variable '" + ident.get_string("Name") + "' of package " +
          object.get_string("pkg"));
    return nil_exprt{};
  }
  symbol_exprt result = symbol->symbol_expr();
  result.add_source_location() = location_of(ident);
  return std::move(result);
}

// -------------------------------------------------------------- functions

void go_convertert::convert_function_body(const irept &decl)
{
  const irept &name = go_field(decl, "Name");
  const irep_idt &object_id = name.get("_obj");
  if(!symbol_table.has_symbol(symbol_id(object_id)))
    return; // declaration failed and was reported
  convert_function(
    symbol_id(object_id),
    parse_tree.object(object_id).get("type"),
    go_field(decl, "Body"),
    location_of(decl));
}

void go_convertert::convert_function(
  const irep_idt &function,
  const irep_idt &signature_index,
  const irept &body_node,
  const source_locationt &location)
{
  state = function_statet{};
  state.function_id = function;
  state.return_type = to_code_type(ns.lookup(function).type).return_type();

  code_blockt body;
  // Go1.27 §Return_statements: "Regardless of how they are declared, all
  // the result values are initialized to the zero values for their type
  // upon entry to the function." Named results are local variables.
  const irept &signature = parse_tree.type_entry(signature_index);
  for(const auto &result : go_list(signature, "results"))
  {
    const irep_idt &result_object = result.get("obj");
    if(result_object.empty())
      continue;
    typet type = convert_type(result.get("type"), body_node);
    if(type.is_nil())
      return;
    const symbol_exprt sym = declare_local(result_object, type, location, body);
    state.named_results.push_back(sym);
    body.add(code_frontend_assignt{sym, *zero_initializer(type, location, ns)});
  }

  code_blockt statements = convert_block(body_node);
  for(auto &s : statements.statements())
    body.add(std::move(s));
  symbol_table.get_writeable_ref(function).value = std::move(body);
}

void go_convertert::convert_package_init(
  const std::vector<irep_idt> &init_functions)
{
  const irep_idt init_id = package_init_id(parse_tree.package);
  state = function_statet{};
  state.function_id = init_id;
  code_blockt body;

  // Go1.27 §Package_initialization: package-level variables are
  // initialised stepwise, each step selecting the earliest variable in
  // declaration order that has no dependencies on uninitialised variables;
  // go/types computes this order (types.Info.InitOrder).
  for(const auto &init : parse_tree.init_order)
  {
    exprt rhs = convert_expr(init.rhs);
    if(rhs.is_nil())
      continue;
    // var a, b = f() initialises several variables from one call.
    std::vector<exprt> values{rhs};
    if(init.lhs.size() > 1)
      values = unpack(rhs, location_of(init.rhs), body);
    for(std::size_t i = 0; i < init.lhs.size(); ++i)
    {
      const irept &object = parse_tree.object(init.lhs[i]);
      if(object.get("name") == "_")
      {
        body.add(expression_statement(values[i], location_of(init.rhs)));
        continue;
      }
      const symbolt *lhs = symbol_table.lookup(symbol_id(init.lhs[i]));
      if(lhs == nullptr)
        continue; // declaration failed and was reported
      code_frontend_assignt assign{
        lhs->symbol_expr(), go_cast(values[i], lhs->type)};
      assign.add_source_location() = location_of(init.rhs);
      body.add(std::move(assign));
    }
  }

  // Go1.27 §Package_initialization: "... followed by calling all init
  // functions in the order they appear in the source, possibly in multiple
  // files, as presented to the compiler."
  for(const auto &init_function : init_functions)
  {
    const symbolt &function = ns.lookup(symbol_id(init_function));
    body.add(code_expressiont{side_effect_expr_function_callt{
      function.symbol_expr(), {}, empty_typet{}, function.location}});
  }

  symbolt symbol = make_symbol(
    init_id,
    "$init",
    code_typet{{}, empty_typet{}},
    source_locationt{},
    parse_tree.package);
  symbol.value = std::move(body);
  add_symbol(symbol);
}

bool go_convertert::operator()()
{
  std::vector<irep_idt> init_functions;
  std::vector<const irept *> functions;

  // Declare all package-level objects first: declarations may be used
  // before they appear (Go1.27 §Declarations_and_scope: the scope of a
  // package-level identifier is the package block).
  for(const auto &file : parse_tree.asts)
  {
    for(const auto &decl : go_list(file, "Decls"))
    {
      if(decl.id() == "FuncDecl")
      {
        const irept &name = go_field(decl, "Name");
        if(name.get("Name") == "init" && go_field(decl, "Recv").is_nil())
          init_functions.push_back(name.get("_obj"));
        declare_function(decl);
        functions.push_back(&decl);
      }
      else if(decl.id() == "GenDecl")
      {
        const irep_idt &tok = decl.get("Tok");
        if(tok == "import" || tok == "const" || tok == "type")
          continue; // resolved and folded by go/types
        for(const auto &spec : go_list(decl, "Specs"))
          for(const auto &name : go_list(spec, "Names"))
            declare_package_var(name.get("_obj"), name);
      }
      else
        unsupported(decl, "declaration " + decl.id_string());
    }
  }

  for(const irept *decl : functions)
    convert_function_body(*decl);
  convert_package_init(init_functions);

  return error_found;
}

// ------------------------------------------------------------- statements

code_blockt go_convertert::convert_block(const irept &block)
{
  code_blockt result;
  result.add_source_location() = location_of(block);
  for(const auto &stmt : go_list(block, "List"))
    convert_statement(stmt, result);
  return result;
}

void go_convertert::convert_statement(const irept &stmt, code_blockt &dest)
{
  const irep_idt &kind = stmt.id();
  if(kind == "BlockStmt")
    dest.add(convert_block(stmt));
  else if(kind == "DeclStmt")
    convert_decl_stmt(stmt, dest);
  else if(kind == "AssignStmt")
    convert_assign(stmt, dest);
  else if(kind == "IncDecStmt")
    convert_inc_dec(stmt, dest);
  else if(kind == "IfStmt")
    convert_if(stmt, dest);
  else if(kind == "ForStmt")
    convert_for(stmt, dest);
  else if(kind == "ReturnStmt")
    convert_return(stmt, dest);
  else if(kind == "ExprStmt")
    convert_expr_stmt(stmt, dest);
  else if(kind == "SwitchStmt")
    convert_switch(stmt, dest);
  else if(kind == "LabeledStmt")
    convert_labeled(stmt, dest);
  else if(kind == "BranchStmt")
    convert_branch(stmt, dest);
  else if(kind == "EmptyStmt")
    return;
  else
    unsupported(stmt, "statement " + id2string(kind));
}

void go_convertert::convert_labeled(const irept &stmt, code_blockt &dest)
{
  // Go1.27 §Labeled_statements: "A labeled statement may be the target of
  // a goto, break or continue statement." Labels are function-scoped
  // (§Label_scopes), so the label name is unique within the function.
  const irept &label = go_field(stmt, "Label");
  const irept &inner = go_field(stmt, "Stmt");
  code_blockt labelled;
  if(inner.id() == "ForStmt")
    convert_for(inner, labelled, label.get("_obj"));
  else if(inner.id() == "SwitchStmt")
    convert_switch(inner, labelled, label.get("_obj"));
  else
    convert_statement(inner, labelled);
  code_labelt result{label.get("Name"), std::move(labelled)};
  result.add_source_location() = location_of(stmt);
  dest.add(std::move(result));
}

void go_convertert::convert_branch(const irept &stmt, code_blockt &dest)
{
  const irep_idt &tok = stmt.get("Tok");
  const irept &label = go_field(stmt, "Label");
  const source_locationt location = location_of(stmt);

  if(tok == "goto")
  {
    // Go1.27 §Goto_statements: go/types has checked that the jump neither
    // enters a block nor skips variable declarations.
    code_gotot jump{label.get("Name")};
    jump.add_source_location() = location;
    return dest.add(std::move(jump));
  }
  if(tok == "fallthrough")
  {
    // Handled by convert_switch; go/types rejects all other uses.
    return unsupported(stmt, "fallthrough in this position");
  }
  if(tok != "break" && tok != "continue")
    return unsupported(stmt, id2string(tok) + " statements");

  // Go1.27 §Break_statements: "A "break" statement terminates execution of
  // the innermost "for", "switch", or "select" statement within the same
  // function." §Continue_statements: "A "continue" statement begins the
  // next iteration of the innermost enclosing "for" loop". With a label,
  // the statement with that label is meant instead.
  const bool is_break = tok == "break";
  const jump_targett *target = nullptr;
  for(auto it = state.jump_targets.rbegin(); it != state.jump_targets.rend();
      ++it)
  {
    const bool matches = label.is_nil() ? (is_break || it->is_loop)
                                        : it->label_object == label.get("_obj");
    if(matches)
    {
      target = &*it;
      break;
    }
  }
  if(target == nullptr)
    return unsupported(stmt, id2string(tok) + " outside its statement");

  // Switch statements are not converted to native switches, so native
  // break and continue reach the innermost loop.
  codet code = !label.is_nil() || !target->is_loop
                 ? codet{code_gotot{
                     is_break ? target->break_label : target->continue_label}}
               : is_break ? codet{code_breakt{}}
                          : codet{code_continuet{}};
  code.add_source_location() = location;
  dest.add(std::move(code));
}

void go_convertert::convert_decl_stmt(const irept &stmt, code_blockt &dest)
{
  const irept &decl = go_field(stmt, "Decl");
  const irep_idt &tok = decl.get("Tok");
  if(tok == "const" || tok == "type")
    return; // folded / resolved by go/types
  for(const auto &spec : go_list(decl, "Specs"))
  {
    const auto &names = go_list(spec, "Names");
    const auto &values = go_list(spec, "Values");
    // Go1.27 §Variable_declarations: the scope of a variable declared in
    // a function begins after its VarSpec, so the initialisers refer to
    // outer objects; go/types has already resolved them by object id.
    std::vector<exprt> initialisers;
    for(const auto &value : values)
      initialisers.push_back(convert_expr(value));
    if(!values.empty() && values.size() != names.size())
    {
      // var a, b = f(): one multi-valued initialiser.
      if(initialisers.front().is_nil())
        continue;
      initialisers = unpack(initialisers.front(), location_of(spec), dest);
    }
    for(std::size_t i = 0; i < names.size(); ++i)
    {
      const irept &name = names[i];
      const irept &object = parse_tree.object(name.get("_obj"));
      typet type = convert_type(object.get("type"), name);
      if(type.is_nil())
        continue;
      if(name.get("Name") == "_")
      {
        if(!values.empty() && initialisers[i].is_not_nil())
          dest.add(expression_statement(initialisers[i], location_of(name)));
        continue;
      }
      symbol_exprt sym =
        declare_local(name.get("_obj"), type, location_of(name), dest);
      // Go1.27 §The_zero_value.
      exprt value = values.empty()
                      ? *zero_initializer(type, location_of(name), ns)
                      : go_cast(initialisers[i], type);
      if(value.is_nil())
        continue;
      code_frontend_assignt assign{sym, value};
      assign.add_source_location() = location_of(name);
      dest.add(std::move(assign));
    }
  }
}

void go_convertert::convert_assign(const irept &stmt, code_blockt &dest)
{
  const irep_idt &tok = stmt.get("Tok");
  const auto &lhs = go_list(stmt, "Lhs");
  const auto &rhs = go_list(stmt, "Rhs");
  const source_locationt &location = location_of(stmt);

  if(tok != "=" && tok != ":=")
  {
    // Go1.27 §Assignment_statements: "x op= y" is "x = x op (y)" but
    // evaluates x only once. Only variables are supported as x so far, so
    // evaluating it twice is equivalent.
    if(lhs.size() != 1 || lhs.front().id() != "Ident")
      return unsupported(stmt, "this assignment target");
    irept binary{"BinaryExpr"};
    binary.add("X") = lhs.front();
    binary.add("Y") = rhs.front();
    const std::string op = id2string(tok);
    binary.set("Op", op.substr(0, op.size() - 1));
    binary.set("_t", lhs.front().get("_t"));
    binary.add(ID_C_source_location) = location;
    exprt target = variable(lhs.front());
    exprt value = convert_expr(binary);
    if(target.is_nil() || value.is_nil())
      return;
    code_frontend_assignt assign{target, value};
    assign.add_source_location() = location;
    dest.add(std::move(assign));
    return;
  }

  // Go1.27 §Assignment_statements: "The assignment proceeds in two phases.
  // First, the operands of index expressions and pointer indirections ...
  // on the left and the expressions on the right are all evaluated in the
  // usual order. Second, the assignments are carried out in left-to-right
  // order." With more than one assignment, evaluate into temporaries first
  // (so that "a, b = b, a" swaps).
  std::vector<exprt> values;
  for(const auto &value : rhs)
    values.push_back(convert_expr(value));
  if(lhs.size() != rhs.size())
  {
    // "In the first, the right hand operand is a single multi-valued
    // expression such as a function call".
    if(values.front().is_nil())
      return;
    values = unpack(values.front(), location, dest);
  }
  else if(lhs.size() > 1)
  {
    for(auto &value : values)
    {
      if(value.is_nil())
        continue;
      symbol_exprt tmp = new_temporary(value.type(), location, dest);
      dest.add(code_frontend_assignt{tmp, value});
      value = tmp;
    }
  }

  for(std::size_t i = 0; i < lhs.size(); ++i)
  {
    const irept &target = lhs[i];
    if(values[i].is_nil())
      continue;
    if(target.id() != "Ident")
    {
      unsupported(target, "assignment to " + target.id_string());
      continue;
    }
    if(target.get("Name") == "_")
    {
      // Go1.27 §Assignment_statements: the blank identifier ignores the
      // value; the expression is still evaluated.
      dest.add(expression_statement(values[i], location));
      continue;
    }
    exprt lvalue;
    if(tok == ":=" && target.get_bool("_def"))
    {
      // Go1.27 §Short_variable_declarations: new variables are declared;
      // others (redeclared) are just assigned.
      const irept &object = parse_tree.object(target.get("_obj"));
      typet type = convert_type(object.get("type"), target);
      if(type.is_nil())
        continue;
      lvalue = declare_local(target.get("_obj"), type, location, dest);
    }
    else
      lvalue = variable(target);
    if(lvalue.is_nil())
      continue;
    code_frontend_assignt assign{lvalue, go_cast(values[i], lvalue.type())};
    assign.add_source_location() = location;
    dest.add(std::move(assign));
  }
}

void go_convertert::convert_inc_dec(const irept &stmt, code_blockt &dest)
{
  // Go1.27 §IncDec_statements: "x++" is "x += 1" (and so wraps on
  // overflow, Go1.27 §Integer_overflow).
  const irept &x = go_field(stmt, "X");
  if(x.id() != "Ident")
    return unsupported(stmt, "this increment/decrement target");
  exprt target = variable(x);
  if(target.is_nil())
    return;
  const exprt one = from_integer(1, target.type());
  exprt value = stmt.get("Tok") == "++" ? exprt{plus_exprt{target, one}}
                                        : exprt{minus_exprt{target, one}};
  code_frontend_assignt assign{target, value};
  assign.add_source_location() = location_of(stmt);
  dest.add(std::move(assign));
}

void go_convertert::convert_if(const irept &stmt, code_blockt &dest)
{
  // Go1.27 §If_statements: the optional simple statement is executed
  // before the condition, in a scope enclosing the whole statement.
  code_blockt scope;
  const irept &init = go_field(stmt, "Init");
  if(init.is_not_nil())
    convert_statement(init, scope);
  exprt cond = convert_expr(go_field(stmt, "Cond"));
  code_blockt then_block = convert_block(go_field(stmt, "Body"));
  codet else_code = codet{ID_nil};
  const irept &else_node = go_field(stmt, "Else");
  if(else_node.id() == "IfStmt")
  {
    code_blockt nested;
    convert_if(else_node, nested);
    else_code = std::move(nested);
  }
  else if(else_node.is_not_nil())
    else_code = convert_block(else_node);
  if(cond.is_nil())
    return;
  code_ifthenelset ite{cond, std::move(then_block)};
  if(else_code.get_statement() != ID_nil)
    ite.else_case() = std::move(else_code);
  ite.add_source_location() = location_of(stmt);
  scope.add(std::move(ite));
  dest.add(std::move(scope));
}

void go_convertert::convert_for(
  const irept &stmt,
  code_blockt &dest,
  const irep_idt &label_object)
{
  // Go1.27 §For_statements, §For_clause: init runs once before the loop;
  // the condition is evaluated before each iteration (absent means true);
  // the post statement runs after each iteration, including when the body
  // executes "continue".
  code_blockt scope;
  const irept &init = go_field(stmt, "Init");
  if(init.is_not_nil())
    convert_statement(init, scope);
  const irept &cond_node = go_field(stmt, "Cond");
  exprt cond =
    cond_node.is_nil() ? exprt{true_exprt{}} : convert_expr(cond_node);
  exprt iter = nil_exprt{};
  const irept &post = go_field(stmt, "Post");
  if(post.is_not_nil())
  {
    code_blockt post_block;
    convert_statement(post, post_block);
    iter = side_effect_expr_statement_expressiont{
      std::move(post_block), empty_typet{}, location_of(post)};
  }

  // A labelled loop may be the target of a break or continue from within
  // a nested loop or switch; these jump after the loop or to the end of
  // the loop body, from where the post statement runs.
  const std::string prefix =
    "$for" + std::to_string(state.label_counter++) + "$";
  state.jump_targets.push_back(
    {label_object, true, prefix + "break", prefix + "continue"});
  code_blockt body = convert_block(go_field(stmt, "Body"));
  state.jump_targets.pop_back();
  if(cond.is_nil())
    return;
  if(!label_object.empty())
    body.add(code_labelt{prefix + "continue", code_skipt{}});
  code_fort loop{code_skipt{}, cond, iter, std::move(body)};
  loop.add_source_location() = location_of(stmt);
  scope.add(std::move(loop));
  if(!label_object.empty())
    scope.add(code_labelt{prefix + "break", code_skipt{}});
  dest.add(std::move(scope));
}

void go_convertert::convert_switch(
  const irept &stmt,
  code_blockt &dest,
  const irep_idt &label_object)
{
  // Go1.27 §Switch_statements, §Expression_switches. The switch statement
  // is its own implicit block, which the optional simple statement starts.
  // "The switch expression is evaluated exactly once", into a temporary;
  // "A missing switch expression is equivalent to the boolean value
  // true." Case expressions "are evaluated left-to-right and
  // top-to-bottom; the first one that equals the switch expression
  // triggers execution of the statements of the associated case". Hence
  // a chain of conditional jumps rather than a native switch, whose case
  // labels would be constants compared in no particular order.
  const source_locationt location = location_of(stmt);
  code_blockt scope;
  const irept &init = go_field(stmt, "Init");
  if(init.is_not_nil())
    convert_statement(init, scope);

  exprt tag = nil_exprt{};
  const irept &tag_node = go_field(stmt, "Tag");
  if(tag_node.is_not_nil())
  {
    exprt value = convert_expr(tag_node);
    if(value.is_nil())
      return;
    symbol_exprt tmp = new_temporary(value.type(), location, scope);
    scope.add(code_frontend_assignt{tmp, value});
    tag = tmp;
  }

  const std::string prefix =
    "$switch" + std::to_string(state.label_counter++) + "$";
  const irep_idt end_label = prefix + "end";
  const auto &clauses = go_list(go_field(stmt, "Body"), "List");
  const auto clause_label = [&prefix](std::size_t i)
  { return irep_idt{prefix + std::to_string(i)}; };

  // Dispatch: test the cases in order; "If no case matches and there is a
  // "default" case, its statements are executed."
  irep_idt default_label = end_label;
  for(std::size_t i = 0; i < clauses.size(); ++i)
  {
    const auto &cases = go_list(clauses[i], "List");
    if(cases.empty())
      default_label = clause_label(i);
    for(const auto &case_node : cases)
    {
      exprt value = convert_expr(case_node);
      if(value.is_nil())
        continue;
      exprt condition =
        tag.is_nil() ? value : convert_comparison("==", value, tag, case_node);
      if(condition.is_nil())
        continue;
      code_ifthenelset dispatch{condition, code_gotot{clause_label(i)}};
      dispatch.add_source_location() = location_of(case_node);
      scope.add(std::move(dispatch));
    }
  }
  scope.add(code_gotot{default_label});

  // Clauses: "Each clause in a "switch" or "select" statement acts as an
  // implicit block." A final "fallthrough" statement continues with the
  // first statement of the next clause; otherwise "control flows to the
  // end of the "switch" statement".
  state.jump_targets.push_back({label_object, false, end_label, irep_idt{}});
  for(std::size_t i = 0; i < clauses.size(); ++i)
  {
    const auto &body = go_list(clauses[i], "Body");
    std::size_t last = body.size();
    while(last > 0 && body[last - 1].id() == "EmptyStmt")
      --last;
    const bool falls_through = last > 0 &&
                               body[last - 1].id() == "BranchStmt" &&
                               body[last - 1].get("Tok") == "fallthrough";
    code_blockt clause;
    clause.add_source_location() = location_of(clauses[i]);
    for(std::size_t j = 0; j < (falls_through ? last - 1 : body.size()); ++j)
      convert_statement(body[j], clause);
    code_labelt labelled{clause_label(i), std::move(clause)};
    labelled.add_source_location() = location_of(clauses[i]);
    scope.add(std::move(labelled));
    if(!falls_through)
      scope.add(code_gotot{end_label});
  }
  state.jump_targets.pop_back();
  scope.add(code_labelt{end_label, code_skipt{}});
  dest.add(std::move(scope));
}

void go_convertert::convert_return(const irept &stmt, code_blockt &dest)
{
  // Go1.27 §Return_statements.
  const auto &results = go_list(stmt, "Results");
  const source_locationt location = location_of(stmt);
  code_frontend_returnt ret;
  if(results.empty() && !state.named_results.empty())
  {
    // "The "return" statement returns the values of these variables."
    if(state.named_results.size() == 1)
      ret = code_frontend_returnt{state.named_results.front()};
    else
    {
      exprt::operandst values{
        state.named_results.begin(), state.named_results.end()};
      ret = code_frontend_returnt{
        struct_exprt{std::move(values), state.return_type}};
    }
  }
  else if(!results.empty())
  {
    // Either one value per result, or "a single call to a multi-valued
    // function. The effect is as if each value returned from that
    // function were assigned to a temporary variable with the type of the
    // respective value, followed by a "return" statement listing these
    // variables".
    std::vector<exprt> values;
    for(const auto &result : results)
      values.push_back(convert_expr(result));
    for(const auto &value : values)
      if(value.is_nil())
        return;
    if(results.size() == 1 && is_tuple(values.front().type()))
      values = unpack(values.front(), location, dest);
    if(values.size() == 1)
    {
      ret = code_frontend_returnt{go_cast(values.front(), state.return_type)};
    }
    else
    {
      const auto &components = to_struct_type(state.return_type).components();
      exprt::operandst operands;
      for(std::size_t i = 0; i < values.size(); ++i)
        operands.push_back(go_cast(values[i], components[i].type()));
      ret = code_frontend_returnt{
        struct_exprt{std::move(operands), state.return_type}};
    }
  }
  ret.add_source_location() = location;
  dest.add(std::move(ret));
}

void go_convertert::convert_expr_stmt(const irept &stmt, code_blockt &dest)
{
  // Go1.27 §Expression_statements: only calls (and receive operations) may
  // appear in statement context.
  const irept &x = go_field(stmt, "X");
  if(x.id() == "CallExpr" && convert_intrinsic_call(x, dest))
    return;
  exprt value = convert_expr(x);
  if(value.is_nil())
    return;
  code_expressiont code{value};
  code.add_source_location() = location_of(stmt);
  dest.add(std::move(code));
}

// ------------------------------------------------------------------ calls

irep_idt go_convertert::callee_object(const irept &call)
{
  const irept *fun = &go_field(call, "Fun");
  while(fun->id() == "ParenExpr" || fun->id() == "IndexExpr" ||
        fun->id() == "IndexListExpr")
    fun = &go_field(*fun, "X");
  if(fun->id() == "SelectorExpr")
    fun = &go_field(*fun, "Sel");
  return fun->id() == "Ident" ? fun->get("_obj") : irep_idt{};
}

void go_convertert::add_panic(
  code_blockt &dest,
  const exprt &condition,
  const std::string &property_class,
  const std::string &comment,
  const source_locationt &location)
{
  source_locationt loc = location;
  loc.set_property_class(property_class);
  loc.set_comment(comment);
  code_assertt check{condition};
  check.add_source_location() = loc;
  dest.add(std::move(check));
  // A panic ends the execution (there is no recover yet): only executions
  // in which it does not happen continue.
  code_assumet stop{condition};
  stop.add_source_location() = location;
  dest.add(std::move(stop));
}

bool go_convertert::convert_intrinsic_call(const irept &call, code_blockt &dest)
{
  const irep_idt callee = callee_object(call);
  const auto &args = go_list(call, "Args");
  const source_locationt &location = location_of(call);

  if(callee == "panic")
  {
    // Go1.27 §Handling_panics: "panic" stops the ordinary execution of
    // the function; without recover, the program terminates.
    std::string comment = "panic";
    if(args.size() == 1 && args.front().get("_cvk") == "string")
    {
      const std::string text = args.front().get_string("_cv");
      comment += ": " + text.substr(1, text.size() - 2); // drop quotes
    }
    else if(args.size() == 1)
    {
      exprt value = convert_expr(args.front());
      if(value.is_nil())
        return true;
      dest.add(expression_statement(value, location));
    }
    add_panic(dest, false_exprt{}, "panic", comment, location);
    return true;
  }

  if(callee == "print" || callee == "println")
  {
    // Go1.27 §Bootstrapping: implementation-specific output; the
    // arguments are evaluated, the output is not modelled.
    for(const auto &arg : args)
    {
      if(arg.get("_cv").empty())
      {
        exprt value = convert_expr(arg);
        if(value.is_not_nil())
          dest.add(expression_statement(value, location));
      }
    }
    return true;
  }

  if(callee == "cprover.Assert")
  {
    exprt cond = convert_expr(args[0]);
    if(cond.is_nil())
      return true;
    if(args[1].get("_cvk") != "string")
    {
      unsupported(args[1], "a non-constant description in cprover.Assert");
      return true;
    }
    const std::string text = args[1].get_string("_cv");
    source_locationt loc = location;
    loc.set_property_class("assertion");
    loc.set_comment(text.substr(1, text.size() - 2));
    code_assertt check{cond};
    check.add_source_location() = loc;
    dest.add(std::move(check));
    return true;
  }

  if(callee == "cprover.Assume")
  {
    exprt cond = convert_expr(args[0]);
    if(cond.is_nil())
      return true;
    code_assumet assume{cond};
    assume.add_source_location() = location;
    dest.add(std::move(assume));
    return true;
  }

  return false;
}

exprt go_convertert::convert_conversion(const irept &call)
{
  // Go1.27 §Conversions: "When converting between integer types, if the
  // value is a signed integer, it is sign extended to implicit infinite
  // precision; otherwise it is zero extended. It is then truncated to fit
  // in the result type's size." This is CBMC's typecast semantics.
  // Constant conversions have already been folded by go/types.
  const auto &args = go_list(call, "Args");
  typet type = expr_type(call);
  exprt value = convert_expr(args.front());
  if(type.is_nil() || value.is_nil())
    return nil_exprt{};
  const bool supported =
    (type.id() == ID_bool && value.type().id() == ID_bool) ||
    ((type.id() == ID_signedbv || type.id() == ID_unsignedbv) &&
     (value.type().id() == ID_signedbv || value.type().id() == ID_unsignedbv));
  if(!supported)
  {
    unsupported(call, "this conversion");
    return nil_exprt{};
  }
  typecast_exprt result{value, type};
  result.add_source_location() = location_of(call);
  return std::move(result);
}

exprt go_convertert::convert_min_max(const irept &call, bool is_min)
{
  // Go1.27 §Min_and_max: the smallest (largest) of the arguments, which
  // have the same ordered type. Integers only so far (floats need NaN and
  // signed-zero handling).
  typet type = expr_type(call);
  if(type.is_nil())
    return nil_exprt{};
  if(type.id() != ID_signedbv && type.id() != ID_unsignedbv)
  {
    unsupported(call, "min/max on non-integer operands");
    return nil_exprt{};
  }
  exprt result = nil_exprt{};
  for(const auto &arg : go_list(call, "Args"))
  {
    exprt value = convert_expr(arg);
    if(value.is_nil())
      return nil_exprt{};
    if(has_subexpr(value, ID_side_effect))
    {
      unsupported(arg, "min/max arguments with side effects");
      return nil_exprt{};
    }
    value = go_cast(value, type);
    if(result.is_nil())
      result = value;
    else
    {
      const exprt keep = is_min
                           ? exprt{binary_relation_exprt{result, ID_le, value}}
                           : exprt{binary_relation_exprt{result, ID_ge, value}};
      result = if_exprt{keep, result, value};
    }
  }
  return result;
}

exprt go_convertert::convert_call(const irept &call)
{
  const irept &fun = go_field(call, "Fun");
  if(fun.get_bool("_tx"))
    return convert_conversion(call);
  if(call.get_bool("_ellipsis"))
  {
    unsupported(call, "calls with \"...\" arguments");
    return nil_exprt{};
  }

  const irep_idt callee = callee_object(call);
  const irept &object = parse_tree.object(callee);
  const source_locationt &location = location_of(call);

  if(callee == "min" || callee == "max")
    return convert_min_max(call, callee == "min");

  if(callee == "cprover.Nondet")
  {
    // Nondet[T]() yields an arbitrary value of T.
    typet type = expr_type(call);
    if(type.is_nil())
      return nil_exprt{};
    if(type.id() == ID_pointer)
    {
      // A function value is nil or refers to a function; an arbitrary
      // pointer would be neither.
      unsupported(call, "cprover.Nondet of function types");
      return nil_exprt{};
    }
    return side_effect_expr_nondett{type, location};
  }

  if(object.get("kind") == "builtin")
  {
    unsupported(call, "built-in function " + id2string(callee));
    return nil_exprt{};
  }
  if(object.get("kind") == "func" && object.get("pkg") != parse_tree.package)
  {
    unsupported(
      call,
      "calls to " + id2string(callee) + " (package " +
        object.get_string("pkg") + ")");
    return nil_exprt{};
  }

  // Go1.27 §Calls: "In a function call, the function value and arguments
  // are evaluated in the usual order. After they are evaluated, new storage
  // is allocated for the function's variables, which includes its
  // parameters and results. Then, the arguments of the call are passed to
  // the function".
  exprt function = convert_expr(fun);
  if(function.is_nil())
    return nil_exprt{};
  const code_typet &code_type =
    to_code_type(to_pointer_type(function.type()).base_type());
  code_blockt prefix;
  exprt::operandst arguments;
  if(convert_arguments(call, code_type, arguments, prefix))
    return nil_exprt{};

  exprt result;
  if(function.id() == ID_address_of)
  {
    // A declared function or function literal: call it directly.
    result = side_effect_expr_function_callt{
      to_address_of_expr(function).object(),
      std::move(arguments),
      code_type.return_type(),
      location};
  }
  else
  {
    // A function value. "Calling a nil function value causes a run-time
    // panic." The check follows the evaluation of the function value and
    // of the arguments, so evaluate those that have side effects first.
    if(has_subexpr(function, ID_side_effect))
    {
      symbol_exprt tmp = new_temporary(function.type(), location, prefix);
      code_frontend_assignt assign{tmp, function};
      assign.add_source_location() = location;
      prefix.add(std::move(assign));
      function = tmp;
    }
    for(auto &argument : arguments)
    {
      if(!has_subexpr(argument, ID_side_effect))
        continue;
      symbol_exprt tmp = new_temporary(argument.type(), location, prefix);
      code_frontend_assignt assign{tmp, argument};
      assign.add_source_location() = location;
      prefix.add(std::move(assign));
      argument = tmp;
    }
    add_panic(
      prefix,
      notequal_exprt{
        function, null_pointer_exprt{to_pointer_type(function.type())}},
      "nil-dereference",
      "runtime error: invalid memory address or nil pointer dereference",
      location);
    // Instructions generated for the call (e.g., by function-pointer
    // removal) take their location from the function expression.
    dereference_exprt callee_expr{function};
    callee_expr.add_source_location() = location;
    result = side_effect_expr_function_callt{
      std::move(callee_expr),
      std::move(arguments),
      code_type.return_type(),
      location};
  }

  if(prefix.statements().empty())
    return result;
  // Statement expressions need a value: a call of a function without
  // results is wrapped as is (its type is void).
  code_expressiont call_code{std::move(result)};
  call_code.add_source_location() = location;
  prefix.add(std::move(call_code));
  return side_effect_expr_statement_expressiont{
    std::move(prefix), code_type.return_type(), location};
}

bool go_convertert::convert_arguments(
  const irept &call,
  const code_typet &code_type,
  exprt::operandst &arguments,
  code_blockt &prefix)
{
  // Go1.27 §Calls: arguments are evaluated in the usual order and assigned
  // to the parameters; go/types guarantees assignability. "As a special
  // case, if the return values of a function or method g are equal in
  // number and individually assignable to the parameters of another
  // function or method f, then the call f(g(parameters_of_g)) will invoke
  // f after passing the return values of g to the parameters of f in
  // order."
  const auto &args = go_list(call, "Args");
  std::vector<exprt> values;
  for(const auto &arg : args)
  {
    exprt value = convert_expr(arg);
    if(value.is_nil())
      return true;
    values.push_back(std::move(value));
  }
  if(values.size() == 1 && is_tuple(values.front().type()))
    values = unpack(values.front(), location_of(call), prefix);
  const auto &parameters = code_type.parameters();
  if(values.size() != parameters.size())
  {
    unsupported(call, "this call");
    return true;
  }
  for(std::size_t i = 0; i < values.size(); ++i)
    arguments.push_back(go_cast(values[i], parameters[i].type()));
  return false;
}

std::vector<exprt> go_convertert::unpack(
  const exprt &value,
  const source_locationt &location,
  code_blockt &dest)
{
  PRECONDITION(is_tuple(value.type()));
  symbol_exprt tmp = new_temporary(value.type(), location, dest);
  code_frontend_assignt assign{tmp, value};
  assign.add_source_location() = location;
  dest.add(std::move(assign));
  std::vector<exprt> values;
  for(const auto &component : to_struct_type(value.type()).components())
    values.push_back(member_exprt{tmp, component});
  return values;
}

/// Collect the ids of the objects that \p node declares (`_def`) and of
/// the variables it refers to, outside nested function literals' own
/// declarations.
static void collect_objects(
  const irept &node,
  std::set<irep_idt> &declared,
  std::vector<const irept *> &uses)
{
  if(node.id() == "Ident" && !node.get("_obj").empty())
  {
    if(node.get_bool("_def"))
      declared.insert(node.get("_obj"));
    else
      uses.push_back(&node);
  }
  for(const auto &sub : node.get_sub())
    collect_objects(sub, declared, uses);
  for(const auto &named : node.get_named_sub())
    collect_objects(named.second, declared, uses);
}

exprt go_convertert::convert_func_lit(const irept &lit)
{
  // Go1.27 §Function_literals: "A function literal represents an anonymous
  // function." It becomes a function of its own. "Function literals are
  // closures: they may refer to variables declared in a surrounding
  // function." Such captured variables are not supported yet.
  std::set<irep_idt> declared;
  std::vector<const irept *> uses;
  collect_objects(lit, declared, uses);
  for(const irept *use : uses)
  {
    const irept &object = parse_tree.object(use->get("_obj"));
    if(
      object.get("kind") == "var" && !object.get_bool("package_level") &&
      declared.find(use->get("_obj")) == declared.end())
    {
      unsupported(
        *use,
        "function literals capturing variables ('" + use->get_string("Name") +
          "')");
      return nil_exprt{};
    }
  }

  const irep_idt &signature_index = lit.get("_t");
  typet type = type_converter.signature(signature_index);
  if(type.is_nil())
  {
    unsupported(lit, type_converter.unsupported_reason);
    return nil_exprt{};
  }
  const std::string base = "$lit" + std::to_string(++state.literal_counter);
  const irep_idt id = id2string(state.function_id) + base;
  const source_locationt location = location_of(lit);
  declare_parameters(
    to_code_type(type), parse_tree.type_entry(signature_index), id, location);
  symbolt symbol = make_symbol(id, base, type, location, parse_tree.package);
  add_symbol(symbol);

  // The literal's body has its own labels, temporaries and results.
  function_statet outer = std::move(state);
  convert_function(id, signature_index, go_field(lit, "Body"), location);
  state = std::move(outer);

  address_of_exprt result{ns.lookup(id).symbol_expr()};
  result.add_source_location() = location;
  return std::move(result);
}

// ------------------------------------------------------------ expressions

exprt go_convertert::convert_constant(const irept &expr, const typet &type)
{
  // Go1.27 §Constants, §Constant_expressions: constant values are exact;
  // go/types has checked that they are representable in their type.
  const irep_idt &kind = expr.get("_cvk");
  const std::string value = expr.get_string("_cv");
  exprt result;
  if(kind == "bool")
    result = value == "true" ? exprt{true_exprt{}} : exprt{false_exprt{}};
  else if(
    kind == "int" && (type.id() == ID_signedbv || type.id() == ID_unsignedbv))
    result = from_integer(string2integer(value), type);
  else
  {
    unsupported(
      expr,
      id2string(kind) + " constants of type " + type.get_string(ID_C_go_type));
    return nil_exprt{};
  }
  result.add_source_location() = location_of(expr);
  return result;
}

exprt go_convertert::guard_panic(
  exprt value,
  const exprt &condition,
  const std::string &property_class,
  const std::string &comment,
  const source_locationt &location)
{
  // The check is part of the expression (a statement expression), so that
  // it is only performed if the operation is evaluated, e.g., not in the
  // unevaluated operand of && (Go1.27 §Logical_operators).
  code_blockt block;
  add_panic(block, condition, property_class, comment, location);
  const typet type = value.type();
  block.add(expression_statement(std::move(value), location));
  return side_effect_expr_statement_expressiont{
    std::move(block), type, location};
}

exprt go_convertert::convert_comparison(
  const irep_idt &op,
  exprt x,
  exprt y,
  const irept &where)
{
  // Go1.27 §Comparison_operators: operands are of identical types (go/types
  // has converted untyped constants), or one is the untyped nil; the
  // result is an (untyped) boolean. "Slice, map, and function types are
  // not comparable. However, as a special case, a slice, map, or function
  // value may be compared to the predeclared identifier nil."
  if(x.id() == ID_go_nil && y.id() != ID_go_nil)
    x = null_pointer_exprt{to_pointer_type(y.type())};
  if(y.id() == ID_go_nil && x.id() != ID_go_nil)
    y = null_pointer_exprt{to_pointer_type(x.type())};
  const bool is_pointer = x.type().id() == ID_pointer;
  if(
    x.type().id() != ID_bool && x.type().id() != ID_signedbv &&
    x.type().id() != ID_unsignedbv && !(is_pointer && op == "==") &&
    !(is_pointer && op == "!="))
  {
    unsupported(
      where, "comparison of type " + x.type().get_string(ID_C_go_type));
    return nil_exprt{};
  }
  y = go_cast(y, x.type());
  const irep_idt rel = op == "=="   ? ID_equal
                       : op == "!=" ? ID_notequal
                       : op == "<"  ? ID_lt
                       : op == "<=" ? ID_le
                       : op == ">"  ? ID_gt
                                    : ID_ge;
  binary_relation_exprt result{x, rel, y};
  result.add_source_location() = location_of(where);
  return std::move(result);
}

exprt go_convertert::convert_unary(const irept &expr)
{
  // Go1.27 §Arithmetic_operators (unary +, -, ^), §Logical_operators (!).
  const irep_idt &op = expr.get("Op");
  exprt x = convert_expr(go_field(expr, "X"));
  if(x.is_nil())
    return nil_exprt{};
  exprt result;
  if(op == "+")
    return x;
  else if(op == "-")
    result = unary_minus_exprt{x}; // "0 - x" for unsigned: wraps
  else if(op == "^")
    result = bitnot_exprt{x}; // Go1.27: "bitwise complement"
  else if(op == "!")
    result = not_exprt{x};
  else
  {
    unsupported(expr, "unary operator " + id2string(op));
    return nil_exprt{};
  }
  result.add_source_location() = location_of(expr);
  return result;
}

exprt go_convertert::convert_binary(const irept &expr)
{
  const irep_idt &op = expr.get("Op");
  const irept &x_node = go_field(expr, "X");
  const irept &y_node = go_field(expr, "Y");
  const source_locationt &location = location_of(expr);

  exprt x = convert_expr(x_node);
  exprt y = convert_expr(y_node);
  if(x.is_nil() || y.is_nil())
    return nil_exprt{};

  // Go1.27 §Logical_operators: "&&" and "||" evaluate the right operand
  // conditionally; goto_convert preserves this for side effects.
  if(op == "&&")
    return and_exprt{x, y};
  if(op == "||")
    return or_exprt{x, y};

  if(
    op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" ||
    op == ">=")
  {
    return convert_comparison(op, x, y, expr);
  }

  // Arithmetic (Go1.27 §Arithmetic_operators). Only integers so far.
  typet type = expr_type(expr);
  if(type.is_nil())
    return nil_exprt{};
  if(type.id() != ID_signedbv && type.id() != ID_unsignedbv)
  {
    unsupported(
      expr,
      "operator " + id2string(op) + " on type " +
        type.get_string(ID_C_go_type));
    return nil_exprt{};
  }
  x = go_cast(x, type);

  const bool is_shift = op == "<<" || op == ">>";
  if(!is_shift)
    y = go_cast(y, type);

  // Operations that may panic: integer division and remainder by zero,
  // and shifts by a negative count. Constant divisors and shift counts
  // need no check: go/types rejects a constant zero divisor and negative
  // constant shift counts at compile time (Go1.27 §Arithmetic_operators,
  // §Constant_expressions). If an operand has side effects, evaluate both
  // operands once, in order, before the check.
  const bool is_division = op == "/" || op == "%";
  const bool constant_y = !y_node.get("_cv").empty();
  const bool may_panic =
    !constant_y &&
    (is_division ||
     (is_shift && type_converter.is_signed_integer(y_node.get("_t"))));
  code_blockt prefix;
  if(
    may_panic &&
    (has_subexpr(x, ID_side_effect) || has_subexpr(y, ID_side_effect)))
  {
    symbol_exprt tx = new_temporary(x.type(), location, prefix);
    prefix.add(code_frontend_assignt{tx, x});
    symbol_exprt ty = new_temporary(y.type(), location, prefix);
    prefix.add(code_frontend_assignt{ty, y});
    x = tx;
    y = ty;
  }

  exprt result;
  if(op == "+")
    result = plus_exprt{x, y}; // Go1.27 §Integer_overflow: wraps
  else if(op == "-")
    result = minus_exprt{x, y};
  else if(op == "*")
    result = mult_exprt{x, y};
  else if(op == "/")
    // Go1.27 §Integer_operators: truncated division; for the most negative
    // value, "x / -1 is equal to x ... due to two's-complement integer
    // overflow". CBMC's div matches both (regression/go/division-semantics).
    result = div_exprt{x, y};
  else if(op == "%")
    result = mod_exprt{x, y}; // truncated: x % y has the sign of x
  else if(op == "&")
    result = bitand_exprt{x, y};
  else if(op == "|")
    result = bitor_exprt{x, y};
  else if(op == "^")
    result = bitxor_exprt{x, y};
  else if(op == "&^")
    result = bitand_exprt{x, bitnot_exprt{y}}; // "bit clear (AND NOT)"
  else if(op == "<<")
    // Go1.27 §Arithmetic_operators: "There is no upper limit on the shift
    // count."; counts of at least the width yield 0 (or -1 for >> of a
    // negative signed value). CBMC's shifts match this
    // (regression/go/shift-semantics).
    result = shl_exprt{x, y};
  else if(op == ">>")
    result = type.id() == ID_signedbv ? exprt{ashr_exprt{x, y}}
                                      : exprt{lshr_exprt{x, y}};
  else
  {
    unsupported(expr, "binary operator " + id2string(op));
    return nil_exprt{};
  }
  result.add_source_location() = location;

  if(!may_panic)
    return result;

  exprt condition;
  std::string property, comment;
  if(is_shift)
  {
    // Go1.27 §Arithmetic_operators: "If the shift count is negative at run
    // time, a run-time panic occurs."
    condition = binary_relation_exprt{y, ID_ge, from_integer(0, y.type())};
    property = "negative-shift";
    comment = "runtime error: negative shift amount";
  }
  else
  {
    // Go1.27 §Integer_operators: "If the divisor is zero at run time, a
    // run-time panic occurs."
    condition = notequal_exprt{y, from_integer(0, y.type())};
    property = "division-by-zero";
    comment = "runtime error: integer divide by zero";
  }
  exprt guarded =
    guard_panic(std::move(result), condition, property, comment, location);
  if(prefix.statements().empty())
    return guarded;
  // Prepend the operand evaluation to the statement expression.
  auto &block = to_code_block(
    to_code(to_side_effect_expr_statement_expression(guarded).statement()));
  for(auto &s : block.statements())
    prefix.add(std::move(s));
  to_side_effect_expr_statement_expression(guarded).statement() = prefix;
  return guarded;
}

exprt go_convertert::convert_expr(const irept &expr)
{
  // Every constant expression has been evaluated exactly by go/types.
  if(!expr.get("_cv").empty() && !expr.get_bool("_tx"))
  {
    typet type = expr_type(expr);
    if(type.is_nil())
      return nil_exprt{};
    return convert_constant(expr, type);
  }

  const irep_idt &kind = expr.id();
  if(
    kind == "Ident" && parse_tree.object(expr.get("_obj")).get("kind") == "nil")
  {
    // Go1.27 §Predeclared_identifiers: go/types records the untyped nil
    // even where it is converted (§Assignability), so the context supplies
    // the pointer type: a typed null replaces this marker in
    // go_cast and convert_comparison.
    exprt result{ID_go_nil, typet{ID_nil}};
    result.add_source_location() = location_of(expr);
    return result;
  }
  if(kind == "Ident")
    return variable(expr);
  if(kind == "FuncLit")
    return convert_func_lit(expr);
  if(kind == "ParenExpr")
    return convert_expr(go_field(expr, "X"));
  if(kind == "UnaryExpr")
    return convert_unary(expr);
  if(kind == "BinaryExpr")
    return convert_binary(expr);
  if(kind == "CallExpr")
    return convert_call(expr);

  unsupported(expr, "expression " + id2string(kind));
  return nil_exprt{};
}
