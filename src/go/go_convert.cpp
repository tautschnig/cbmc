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
#include <util/std_code.h>
#include <util/symbol_table_base.h>

#include "go_parse_tree.h"

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

source_locationt go_convertert::location_of(const irept &node) const
{
  // Property and loop ids are derived from the function recorded in the
  // source location (CBMC convention: the function's symbol id).
  source_locationt location = go_location(node);
  if(!function_id.empty() && location.is_not_nil())
    location.set_function(function_id);
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

void go_convertert::declare_function(const irept &decl)
{
  if(go_field(decl, "Recv").is_not_nil())
    return unsupported(decl, "methods");
  if(go_field(decl, "Body").is_nil())
    return unsupported(decl, "functions without a body");
  const irept &name = go_field(decl, "Name");
  const irep_idt &object_id = name.get("_obj");
  const irept &object = parse_tree.object(object_id);
  const irept &signature = parse_tree.type_entry(object.get("type"));
  if(go_field(go_field(decl, "Type"), "TypeParams").is_not_nil())
    return unsupported(decl, "generic functions");

  typet type = convert_type(object.get("type"), decl);
  if(type.is_nil())
    return;
  code_typet &code_type = to_code_type(type);

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
      param_object = id2string(object_id) + "::$param" + std::to_string(index);
      base = "$param" + std::to_string(index);
    }
    const irep_idt param_id = symbol_id(param_object);
    parameter.set_identifier(param_id);
    parameter.set_base_name(base);
    symbolt param_symbol = make_symbol(
      param_id, base, parameter.type(), location_of(decl), parse_tree.package);
    param_symbol.is_parameter = true;
    param_symbol.is_lvalue = true;
    param_symbol.is_thread_local = true;
    param_symbol.is_file_local = true;
    add_symbol(param_symbol);
    ++index;
  }

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
  const irep_idt base = "$tmp" + std::to_string(tmp_counter++);
  symbolt symbol = make_symbol(
    id2string(function_id) + "::" + id2string(base),
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
  symbolt *symbol = symbol_table.get_writeable(symbol_id(object_id));
  if(symbol == nullptr)
    return; // declaration failed and was reported
  function_id = symbol->name;
  tmp_counter = 0;
  return_type = to_code_type(symbol->type).return_type();
  named_results.clear();

  code_blockt body;
  // Go1.27 §Function_types, §Return_statements: named result parameters
  // are local variables initialised to their zero value.
  const irept &signature =
    parse_tree.type_entry(parse_tree.object(object_id).get("type"));
  for(const auto &result : go_list(signature, "results"))
  {
    const irep_idt &result_object = result.get("obj");
    if(result_object.empty())
      continue;
    const symbol_exprt sym =
      declare_local(result_object, return_type, location_of(decl), body);
    named_results.push_back(sym);
    body.add(code_frontend_assignt{
      sym, *zero_initializer(return_type, location_of(decl), ns)});
  }

  code_blockt statements = convert_block(go_field(decl, "Body"));
  for(auto &s : statements.statements())
    body.add(std::move(s));
  symbol = symbol_table.get_writeable(function_id);
  symbol->value = std::move(body);
}

void go_convertert::convert_package_init(
  const std::vector<irep_idt> &init_functions)
{
  const irep_idt init_id = package_init_id(parse_tree.package);
  function_id = init_id;
  tmp_counter = 0;
  code_blockt body;

  // Go1.27 §Package_initialization: package-level variables are
  // initialised stepwise, each step selecting the earliest variable in
  // declaration order that has no dependencies on uninitialised variables;
  // go/types computes this order (types.Info.InitOrder).
  for(const auto &init : parse_tree.init_order)
  {
    if(init.lhs.size() != 1)
    {
      unsupported(init.rhs, "initialising several variables from one call");
      continue;
    }
    exprt rhs = convert_expr(init.rhs);
    if(rhs.is_nil())
      continue;
    const irept &object = parse_tree.object(init.lhs.front());
    if(object.get("name") == "_")
    {
      body.add(code_expressiont{rhs});
      continue;
    }
    const symbolt &lhs = ns.lookup(symbol_id(init.lhs.front()));
    code_frontend_assignt assign{
      lhs.symbol_expr(), typecast_exprt::conditional_cast(rhs, lhs.type)};
    assign.add_source_location() = location_of(init.rhs);
    body.add(std::move(assign));
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
  else if(kind == "EmptyStmt")
    return;
  else if(kind == "BranchStmt")
  {
    // Go1.27 §Break_statements, §Continue_statements.
    const irep_idt &tok = stmt.get("Tok");
    if(go_field(stmt, "Label").is_not_nil())
      return unsupported(stmt, "labelled " + id2string(tok));
    codet branch = tok == "break"      ? codet{code_breakt{}}
                   : tok == "continue" ? codet{code_continuet{}}
                                       : codet{ID_nil};
    if(branch.get_statement() == ID_nil)
      return unsupported(stmt, id2string(tok) + " statements");
    branch.add_source_location() = location_of(stmt);
    dest.add(std::move(branch));
  }
  else
    unsupported(stmt, "statement " + id2string(kind));
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
    if(!values.empty() && values.size() != names.size())
    {
      unsupported(spec, "initialising several variables from one call");
      continue;
    }
    // Go1.27 §Variable_declarations: the scope of a variable declared in
    // a function begins after its VarSpec, so the initialisers refer to
    // outer objects; go/types has already resolved them by object id.
    std::vector<exprt> initialisers;
    for(const auto &value : values)
      initialisers.push_back(convert_expr(value));
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
          dest.add(code_expressiont{initialisers[i]});
        continue;
      }
      symbol_exprt sym =
        declare_local(name.get("_obj"), type, location_of(name), dest);
      // Go1.27 §The_zero_value.
      exprt value = values.empty()
                      ? *zero_initializer(type, location_of(name), ns)
                      : typecast_exprt::conditional_cast(initialisers[i], type);
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

  if(lhs.size() != rhs.size())
    return unsupported(stmt, "assigning the results of a multi-valued call");

  // Go1.27 §Assignment_statements: "The assignment proceeds in two phases.
  // First, the operands of index expressions and pointer indirections ...
  // on the left and the expressions on the right are all evaluated in the
  // usual order. Second, the assignments are carried out in left-to-right
  // order." With more than one assignment, evaluate into temporaries first
  // (so that "a, b = b, a" swaps).
  std::vector<exprt> values;
  for(const auto &value : rhs)
    values.push_back(convert_expr(value));
  if(lhs.size() > 1)
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
      dest.add(code_expressiont{values[i]});
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
    code_frontend_assignt assign{
      lvalue, typecast_exprt::conditional_cast(values[i], lvalue.type())};
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

void go_convertert::convert_for(const irept &stmt, code_blockt &dest)
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
  code_blockt body = convert_block(go_field(stmt, "Body"));
  if(cond.is_nil())
    return;
  code_fort loop{code_skipt{}, cond, iter, std::move(body)};
  loop.add_source_location() = location_of(stmt);
  scope.add(std::move(loop));
  dest.add(std::move(scope));
}

void go_convertert::convert_return(const irept &stmt, code_blockt &dest)
{
  // Go1.27 §Return_statements.
  const auto &results = go_list(stmt, "Results");
  code_frontend_returnt ret;
  if(results.size() == 1)
  {
    exprt value = convert_expr(results.front());
    if(value.is_nil())
      return;
    ret = code_frontend_returnt{
      typecast_exprt::conditional_cast(value, return_type)};
  }
  else if(results.size() > 1)
    return unsupported(stmt, "returning several results");
  else if(!named_results.empty())
  {
    // A "return" without results in a function with named results
    // returns their current values.
    ret = code_frontend_returnt{named_results.front()};
  }
  ret.add_source_location() = location_of(stmt);
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
      dest.add(code_expressiont{value});
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
          dest.add(code_expressiont{value});
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
    value = typecast_exprt::conditional_cast(value, type);
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
    return side_effect_expr_nondett{type, location};
  }

  if(object.get("kind") == "builtin")
  {
    unsupported(call, "built-in function " + id2string(callee));
    return nil_exprt{};
  }
  if(object.get("kind") != "func" || callee.empty())
  {
    unsupported(call, "calls of function values");
    return nil_exprt{};
  }
  const symbolt *function = symbol_table.lookup(symbol_id(callee));
  if(function == nullptr)
  {
    unsupported(
      call,
      "calls to " + id2string(callee) + " (package " +
        object.get_string("pkg") + ")");
    return nil_exprt{};
  }

  // Go1.27 §Calls: arguments are evaluated in the usual order and assigned
  // to the parameters; go/types guarantees assignability.
  const code_typet &code_type = to_code_type(function->type);
  exprt::operandst arguments;
  std::size_t index = 0;
  for(const auto &arg : go_list(call, "Args"))
  {
    exprt value = convert_expr(arg);
    if(value.is_nil())
      return nil_exprt{};
    arguments.push_back(typecast_exprt::conditional_cast(
      value, code_type.parameters()[index++].type()));
  }
  side_effect_expr_function_callt result{
    function->symbol_expr(),
    std::move(arguments),
    code_type.return_type(),
    location};
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
  block.add(code_expressiont{std::move(value)});
  return side_effect_expr_statement_expressiont{
    std::move(block), type, location};
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

  // Go1.27 §Comparison_operators: operands are of identical types (go/types
  // has converted untyped constants); the result is an (untyped) boolean.
  if(
    op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" ||
    op == ">=")
  {
    if(
      x.type().id() != ID_bool && x.type().id() != ID_signedbv &&
      x.type().id() != ID_unsignedbv)
    {
      unsupported(
        expr, "comparison of type " + x.type().get_string(ID_C_go_type));
      return nil_exprt{};
    }
    y = typecast_exprt::conditional_cast(y, x.type());
    const irep_idt rel = op == "=="   ? ID_equal
                         : op == "!=" ? ID_notequal
                         : op == "<"  ? ID_lt
                         : op == "<=" ? ID_le
                         : op == ">"  ? ID_gt
                                      : ID_ge;
    binary_relation_exprt result{x, rel, y};
    result.add_source_location() = location;
    return std::move(result);
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
  x = typecast_exprt::conditional_cast(x, type);

  const bool is_shift = op == "<<" || op == ">>";
  if(!is_shift)
    y = typecast_exprt::conditional_cast(y, type);

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
  if(kind == "Ident")
    return variable(expr);
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
