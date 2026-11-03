/*******************************************************************\

Module: Go front-end: range over functions

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// Range over functions (Go1.27 §For_range): "For a function f, the
/// iteration proceeds by calling f with a new, synthesized yield function as
/// its argument. If yield is called before f returns, the arguments to
/// yield become the iteration values for executing the loop body once.
/// After each successive loop iteration, yield returns true and may be
/// called again to continue the loop. As long as the loop body does not
/// terminate, the "range" clause will continue to generate iteration values
/// this way for each yield call until f returns. If the loop body
/// terminates (such as by a break statement), yield returns false and must
/// not be called again."
///
/// The body becomes the yield function, a closure that shares the
/// variables of the enclosing function it uses with it (as a function
/// literal does), and three more with the loop: the state of the iteration
/// (ready for the next call of yield, the body ended the loop, the loop
/// ended, or the body is running), why the body ended the loop (break,
/// return, or a break or continue statement whose target is outside the
/// loop), and the results of a return statement. After f returns, the loop
/// does what the body asked for. As gc does, calls of yield after the body
/// ended the loop, after the loop ended, or after a panic of the body that
/// f recovered, are run-time errors.

#include <util/arith_tools.h>
#include <util/c_types.h>
#include <util/config.h>
#include <util/expr_initializer.h>
#include <util/pointer_expr.h>
#include <util/std_code.h>
#include <util/symbol.h>

#include "go_convert.h"
#include "go_parse_tree.h"

/// States of the iteration of a range over a function.
enum range_statet
{
  RANGE_READY = 0,  // yield may be called
  RANGE_DONE = 1,   // the body ended the loop
  RANGE_EXITED = 2, // the loop ended
  RANGE_RUNNING = 3 // the body is running (or panicked)
};

/// A function-local symbol of the yield function (as declare_parameters'
/// and convert_func_lit's).
static symbolt make_local_symbol(
  const irep_idt &id,
  const irep_idt &base_name,
  const typet &type,
  const source_locationt &location,
  const irep_idt &module)
{
  symbolt symbol{id, type, "go"};
  symbol.base_name = base_name;
  symbol.pretty_name = base_name;
  symbol.module = module;
  symbol.location = location;
  return symbol;
}

static code_expressiont
expression_statement(exprt value, const source_locationt &location)
{
  code_expressiont result{std::move(value)};
  result.add_source_location() = location;
  return result;
}

static const typet &range_int_type()
{
  static const signedbv_typet type{32};
  return type;
}

/// The labels that \p node declares.
static void collect_labels(const irept &node, std::set<irep_idt> &labels)
{
  if(node.id() == "LabeledStmt")
    labels.insert(go_field(node, "Label").get("_obj"));
  for(const auto &sub : node.get_sub())
    collect_labels(sub, labels);
  for(const auto &named : node.get_named_sub())
    collect_labels(named.second, labels);
}

void go_convertert::range_body_exit(
  std::size_t exit,
  const source_locationt &location,
  code_blockt &dest)
{
  PRECONDITION(state.range_body);
  const auto shared = [&](const irep_idt &id)
  { return dereference_exprt{state.heap_pointers.at(id)}; };
  const bool next = exit == RANGE_EXIT_NONE;
  code_frontend_assignt set_state{
    shared(state.range_body->state_id),
    from_integer(next ? RANGE_READY : RANGE_DONE, range_int_type())};
  set_state.add_source_location() = location;
  dest.add(std::move(set_state));
  if(!next)
  {
    code_frontend_assignt set_exit{
      shared(state.range_body->exit_id), from_integer(exit, range_int_type())};
    set_exit.add_source_location() = location;
    dest.add(std::move(set_exit));
  }
  // "After each successive loop iteration, yield returns true"; "If the
  // loop body terminates (such as by a break statement), yield returns
  // false".
  code_frontend_returnt ret{assign_cast(
    next ? exprt{true_exprt{}} : exprt{false_exprt{}}, state.return_type)};
  ret.add_source_location() = location;
  dest.add(std::move(ret));
}

void go_convertert::range_body_branch(const irept &stmt, code_blockt &dest)
{
  // A break or continue statement without target in the body: the loop
  // itself (with no label, or its label), or a statement around it, which
  // the loop leaves to after f returns.
  const bool is_break = stmt.get("Tok") == "break";
  const irept &label = go_field(stmt, "Label");
  const source_locationt location = location_of(stmt);
  if(label.is_nil() || label.get("_obj") == state.range_body->label_object)
  {
    return range_body_exit(
      is_break ? RANGE_EXIT_BREAK : RANGE_EXIT_NONE, location, dest);
  }
  state.range_body->branches->push_back(stmt);
  range_body_exit(
    first_branch_exit + state.range_body->branches->size() - 1, location, dest);
}

void go_convertert::convert_range_func(
  const irept &stmt,
  const exprt &function,
  code_blockt &dest,
  const irep_idt &label_object)
{
  const source_locationt location = location_of(stmt);
  const irept &x_node = go_field(stmt, "X");
  const irept &signature = type_converter.underlying(x_node.get("_t"));
  const auto &params = go_list(signature, "params");
  if(params.size() != 1)
    return unsupported(stmt, "this range over a function");
  const irep_idt yield_signature = params.front().get("type");

  code_blockt scope;
  const exprt f = evaluate_once(function, location, scope);

  // The variables the body shares with the loop, anew for each execution
  // of the loop. The results are those of the function containing the
  // loop (the outermost one for nested loops).
  const typet return_type =
    state.range_body ? state.range_body->return_type : state.return_type;
  const symbol_exprt state_ptr = new_heap_variable(
    range_int_type(),
    from_integer(RANGE_READY, range_int_type()),
    location,
    scope);
  const symbol_exprt exit_ptr = new_heap_variable(
    range_int_type(),
    from_integer(RANGE_EXIT_NONE, range_int_type()),
    location,
    scope);
  std::optional<symbol_exprt> results_ptr;
  if(return_type.id() != ID_empty)
    results_ptr = new_heap_variable(return_type, nil_exprt{}, location, scope);

  // The yield function.
  typet type = type_converter.signature(yield_signature);
  if(type.is_nil())
    return unsupported(stmt, type_converter.unsupported_reason);
  const std::string base = "$range" + std::to_string(++state.literal_counter);
  const irep_idt id = id2string(state.function_id) + base;
  code_typet &code_type = to_code_type(type);
  declare_parameters(
    code_type, parse_tree.type_entry(yield_signature), id, location);
  const irep_idt env_id = id2string(id) + "::$env";
  code_typet::parametert env_param{
    typet{go_type_convertert::env_pointer_type()}};
  env_param.set_identifier(env_id);
  env_param.set_base_name("$env");
  code_type.parameters().insert(code_type.parameters().begin(), env_param);
  symbolt env_symbol = make_local_symbol(
    env_id, "$env", env_param.type(), location, parse_tree.package);
  env_symbol.is_parameter = true;
  env_symbol.is_lvalue = true;
  env_symbol.is_thread_local = true;
  env_symbol.is_file_local = true;
  add_symbol(env_symbol);
  add_symbol(make_local_symbol(id, base, type, location, parse_tree.package));

  // Its environment: the variables of the enclosing function it uses (as
  // for function literals), then the shared ones.
  std::vector<irep_idt> captured = range_body_variables(stmt, parse_tree);
  std::vector<typet> captured_types;
  exprt::operandst pointers;
  for(const auto &object : captured)
  {
    const auto heap = state.heap_pointers.find(object);
    if(heap == state.heap_pointers.end())
    {
      return unsupported(
        stmt,
        "using '" + parse_tree.object(object).get_string("name") +
          "' in the body of this range over a function");
    }
    pointers.push_back(heap->second);
    captured_types.push_back(to_pointer_type(heap->second.type()).base_type());
  }
  auto body = std::make_shared<range_bodyt>();
  body->state_id = id2string(id) + "::$state";
  body->exit_id = id2string(id) + "::$exit";
  body->results_id = id2string(id) + "::$results";
  body->return_type = return_type;
  body->label_object = label_object;
  collect_labels(go_field(stmt, "Body"), body->labels);
  std::vector<irept> branches;
  body->branches = &branches;
  captured.push_back(body->state_id);
  captured_types.push_back(range_int_type());
  pointers.push_back(state_ptr);
  captured.push_back(body->exit_id);
  captured_types.push_back(range_int_type());
  pointers.push_back(exit_ptr);
  if(results_ptr.has_value())
  {
    captured.push_back(body->results_id);
    captured_types.push_back(return_type);
    pointers.push_back(*results_ptr);
  }

  // The start of the body: check that the iteration may continue, then
  // bind the iteration values (the arguments of yield) "as in an
  // assignment statement", or to new variables of each iteration
  // [Go 1.22].
  const auto prologue = [&](code_blockt &code)
  {
    const exprt iteration_state =
      dereference_exprt{state.heap_pointers.at(state.range_body->state_id)};
    const std::pair<range_statet, const char *> errors[] = {
      {RANGE_DONE,
       "runtime error: range function continued iteration after function "
       "for loop body returned false"},
      {RANGE_EXITED,
       "runtime error: range function continued iteration after whole loop "
       "exit"},
      {RANGE_RUNNING,
       "runtime error: range function continued iteration after loop body "
       "panic"}};
    for(const auto &error : errors)
    {
      add_panic(
        code,
        notequal_exprt{
          iteration_state, from_integer(error.first, range_int_type())},
        "range-func",
        error.second,
        location);
    }
    code.add(code_frontend_assignt{
      iteration_state, from_integer(RANGE_RUNNING, range_int_type())});
    const std::vector<exprt> arguments = parameter_symbols(ns.lookup(id));
    std::size_t index = 1; // after the environment
    for(const char *field : {"Key", "Value"})
    {
      const irept &node = go_field(stmt, field);
      if(node.is_nil())
        continue;
      const exprt value = arguments.at(index++);
      if(node.id() == "Ident" && node.get("Name") == "_")
        continue;
      exprt target;
      if(stmt.get("Tok") == ":=")
      {
        typet var_type =
          convert_type(parse_tree.object(node.get("_obj")).get("type"), node);
        if(var_type.is_nil())
          return;
        target = declare_local(node.get("_obj"), var_type, location, code);
      }
      else
      {
        access_partst parts;
        target = convert_access(node, parts);
        if(target.is_nil())
          return;
        for(auto &statement : parts.operands.statements())
          code.add(std::move(statement));
        for(auto &statement : parts.checks.statements())
          code.add(std::move(statement));
      }
      code_frontend_assignt assign{target, assign_cast(value, target.type())};
      assign.add_source_location() = location_of(node);
      code.add(std::move(assign));
    }
  };

  function_statet outer = std::move(state);
  convert_function(
    id,
    yield_signature,
    go_field(stmt, "Body"),
    location,
    captured,
    &captured_types,
    body,
    prologue);
  state = std::move(outer);

  // f(yield)
  const typet yield_type = convert_type(yield_signature, stmt);
  if(yield_type.is_nil())
    return;
  const struct_typet env_type = [&]
  {
    struct_typet::componentst components;
    for(std::size_t i = 0; i < pointers.size(); ++i)
      components.emplace_back("c" + std::to_string(i), pointers[i].type());
    return struct_typet{std::move(components)};
  }();
  const symbol_exprt env = new_heap_variable(
    env_type, struct_exprt{std::move(pointers), env_type}, location, scope);
  const auto &yield_components = to_struct_type(yield_type).components();
  const struct_exprt yield{
    {assign_cast(
       address_of_exprt{ns.lookup(id).symbol_expr()},
       yield_components[0].type()),
     typecast_exprt{env, yield_components[1].type()}},
    yield_type};
  const auto &f_components =
    to_struct_type(go_follow(ns, f.type())).components();
  const member_exprt f_code{f, f_components[0]};
  // "Calling a nil function value causes a run-time panic."
  add_panic(
    scope,
    notequal_exprt{f_code, null_pointer_exprt{to_pointer_type(f_code.type())}},
    "nil-dereference",
    "runtime error: invalid memory address or nil pointer dereference",
    location);
  dereference_exprt callee{f_code};
  callee.add_source_location() = location;
  side_effect_expr_function_callt call{
    std::move(callee),
    {member_exprt{f, f_components[1]}, yield},
    empty_typet{},
    location};
  scope.add(expression_statement(std::move(call), location));

  // f returned: a panic of the body that f recovered is an error. Then the
  // loop has ended, and does what the body asked for.
  const dereference_exprt iteration_state{state_ptr};
  add_panic(
    scope,
    notequal_exprt{
      iteration_state, from_integer(RANGE_RUNNING, range_int_type())},
    "range-func",
    "runtime error: range function recovered a loop body panic and did not "
    "resume panicking",
    location);
  scope.add(code_frontend_assignt{
    iteration_state, from_integer(RANGE_EXITED, range_int_type())});
  const dereference_exprt exit{exit_ptr};
  const auto on_exit = [&](std::size_t code, code_blockt then)
  {
    code_ifthenelset check{
      equal_exprt{exit, from_integer(code, range_int_type())}, std::move(then)};
    check.add_source_location() = location;
    scope.add(std::move(check));
  };
  if(results_ptr.has_value())
  {
    code_blockt then;
    emit_return(
      code_frontend_returnt{dereference_exprt{*results_ptr}}, location, then);
    on_exit(RANGE_EXIT_RETURN_VALUES, std::move(then));
  }
  // A return statement without values only exists for functions without
  // results or with named results.
  if(
    state.range_body || return_type.id() == ID_empty ||
    !state.named_results.empty())
  {
    code_blockt then;
    emit_return(bare_return(), location, then);
    on_exit(RANGE_EXIT_RETURN, std::move(then));
  }
  for(std::size_t i = 0; i < branches.size(); ++i)
  {
    code_blockt then;
    convert_branch(branches[i], then);
    on_exit(first_branch_exit + i, std::move(then));
  }
  dest.add(std::move(scope));
}
