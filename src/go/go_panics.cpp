/*******************************************************************\

Module: Go front-end: panics, deferred calls and recover

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// Run-time panics and calls of panic (Go1.27 §Run_time_panics,
/// §Handling_panics), defer statements (§Defer_statements) and recover.
///
/// A panic records its value and its site in global variables, sets
/// `go::$panicking` and jumps to the end of the function (label `$unwind`),
/// where the deferred calls run and the function returns. After every call
/// of a Go function, the caller continues unwinding if `go::$panicking` is
/// set. An effective call of recover clears it, so that the deferred
/// function and then its caller continue normally. When the top-level
/// function returns, `go::$check_panics` has one property per panic site,
/// which fails if that panic ended the program.

#include <util/arith_tools.h>
#include <util/c_types.h>
#include <util/config.h>
#include <util/cprover_prefix.h>
#include <util/expr_initializer.h>
#include <util/pointer_expr.h>
#include <util/prefix.h>
#include <util/std_code.h>
#include <util/symbol_table_base.h>

#include "go_convert.h"
#include "go_parse_tree.h"

/// Name of the flag of calls after which add_panic_checks does not unwind
/// (the calls that start a goroutine).
#define ID_C_go_no_unwind "#go_no_unwind"

#define UNWIND_LABEL "$unwind"

symbol_exprt go_convertert::panicking()
{
  return symbol_exprt{"go::$panicking", bool_typet{}};
}

symbol_exprt go_convertert::panic_value()
{
  return symbol_exprt{"go::$panic_value", go_type_convertert::interface_type()};
}

symbol_exprt go_convertert::panic_site()
{
  return symbol_exprt{"go::$panic_site", signedbv_typet{32}};
}

symbol_exprt go_convertert::defer_token()
{
  return symbol_exprt{"go::$defer_token", bool_typet{}};
}

const irep_idt go_convertert::check_panics_id = "go::$check_panics";

void go_convertert::declare_panic_state()
{
  // Declared early, as go statements call it.
  declare_generated(symbol_table, check_panics_id, {}, empty_typet{});
  // The state of the (only) goroutine: thread-local, as goroutines will be
  // threads.
  for(const symbol_exprt &variable :
      {panicking(), panic_value(), panic_site(), defer_token()})
  {
    if(symbol_table.has_symbol(variable.get_identifier()))
      continue;
    symbolt symbol{variable.get_identifier(), variable.type(), "go"};
    const std::string name = id2string(variable.get_identifier());
    symbol.base_name = name.substr(4);
    symbol.pretty_name = symbol.base_name;
    symbol.is_static_lifetime = true;
    symbol.is_lvalue = true;
    symbol.is_thread_local = true;
    symbol.module = parse_tree.package;
    auto zero = zero_initializer(variable.type(), source_locationt{}, ns);
    CHECK_RETURN(zero.has_value());
    symbol.value = *zero;
    symbol_table.add(symbol);
  }
}

exprt go_convertert::runtime_error(
  const std::string &message,
  const source_locationt &location)
{
  // Go1.27 §Run_time_panics: "Execution errors such as attempting to index
  // an array out of bounds trigger a run-time panic equivalent to a call
  // of the built-in function panic with a value of the
  // implementation-defined interface type runtime.Error. That type
  // satisfies the predeclared interface type error." The value is a
  // cprover/rt.Error (runtime/rt.go) with the message gc uses.
  const irept &object = parse_tree.object("cprover/rt.Error");
  const typet type = type_converter(object.get("type"));
  INVARIANT(type.is_not_nil(), "the runtime package declares Error");
  return box(
    struct_exprt{{string_constant(message, location)}, type}, location);
}

void go_convertert::raise_panic(
  code_blockt &dest,
  const exprt &value,
  const std::string &property_class,
  const std::string &comment,
  const source_locationt &location)
{
  source_locationt site = location;
  if(site.get_function().empty())
    site.set_function(state.function_id);
  site.set_property_class(property_class);
  site.set_comment(comment);
  panic_sites.push_back(site);

  // Go1.27 §Handling_panics: "While executing a function F, an explicit
  // call to panic or a run-time panic terminates the execution of F. Any
  // functions deferred by F are then executed as usual."
  const auto assign = [&](const exprt &lhs, const exprt &rhs)
  {
    code_frontend_assignt result{lhs, rhs};
    result.add_source_location() = location;
    dest.add(std::move(result));
  };
  // The value is only observable through recover.
  if(uses_recover)
    assign(panic_value(), value);
  assign(panic_site(), from_integer(panic_sites.size(), panic_site().type()));
  assign(panicking(), true_exprt{});
  // Unwinding leaves the atomic sections of the models (rt.AtomicBegin):
  // the paths that join at the end of the function must agree on them.
  for(std::size_t i = 0; i < state.atomic_depth; ++i)
  {
    codet end{ID_atomic_end};
    end.add_source_location() = location;
    dest.add(std::move(end));
  }
  code_gotot jump{UNWIND_LABEL};
  jump.add_source_location() = location;
  dest.add(std::move(jump));
}

void go_convertert::add_panic(
  code_blockt &dest,
  const exprt &condition,
  const std::string &property_class,
  const std::string &comment,
  const source_locationt &location)
{
  code_blockt panic;
  raise_panic(
    panic,
    uses_recover ? runtime_error(comment, location) : exprt{nil_exprt{}},
    property_class,
    comment,
    location);
  if(condition.is_false())
  {
    for(auto &statement : panic.statements())
      dest.add(std::move(statement));
    return;
  }
  code_ifthenelset check{not_exprt{condition}, std::move(panic)};
  check.add_source_location() = location;
  dest.add(std::move(check));
}

/// Whether \p expr is a call of a Go function (directly or through a
/// function value), after which the caller may have to unwind.
static bool is_go_call(const exprt &expr)
{
  if(
    expr.id() != ID_side_effect ||
    to_side_effect_expr(expr).get_statement() != ID_function_call)
  {
    return false;
  }
  if(expr.get_bool(ID_C_go_no_unwind))
    return false;
  const exprt &function = to_side_effect_expr_function_call(expr).function();
  return function.id() != ID_symbol ||
         has_prefix(
           id2string(to_symbol_expr(function).get_identifier()), "go::");
}

static code_expressiont
expression_statement(exprt value, const source_locationt &location)
{
  code_expressiont result{std::move(value)};
  result.add_source_location() = location;
  return result;
}

static codet
unwind_if_panicking(const exprt &panicking, const source_locationt &location)
{
  code_gotot jump{UNWIND_LABEL};
  jump.add_source_location() = location;
  code_ifthenelset check{panicking, std::move(jump)};
  check.add_source_location() = location;
  return std::move(check);
}

void go_convertert::add_panic_checks(exprt &expr)
{
  // Go1.27 §Handling_panics: "Next, any deferred functions run by F's
  // caller are run, and so on up to any deferred by the top-level function
  // in the executing goroutine."
  if(expr.id() == ID_code)
  {
    codet &code = to_code(expr);
    // A call statement (but one with a value may be the value of a
    // statement expression, which must stay an expression).
    if(
      code.get_statement() == ID_expression && is_go_call(code.op0()) &&
      code.op0().type().id() == ID_empty)
    {
      for(auto &operand : code.op0().operands())
        add_panic_checks(operand);
      const source_locationt location = code.source_location();
      code_blockt block{{code, unwind_if_panicking(panicking(), location)}};
      expr = std::move(block);
      return;
    }
    for(auto &operand : code.operands())
      add_panic_checks(operand);
    return;
  }
  for(auto &operand : expr.operands())
    add_panic_checks(operand);
  if(!is_go_call(expr))
    return;

  // The call's value, available only if the callee returned normally.
  const source_locationt location = expr.source_location();
  const typet type = expr.type();
  code_blockt block;
  if(type.id() == ID_empty)
  {
    block.add(code_expressiont{std::move(expr)});
    block.add(unwind_if_panicking(panicking(), location));
  }
  else
  {
    const symbol_exprt result = new_temporary(type, location, block);
    code_frontend_assignt assign{result, std::move(expr)};
    assign.add_source_location() = location;
    block.add(std::move(assign));
    block.add(unwind_if_panicking(panicking(), location));
    block.add(expression_statement(result, location));
  }
  expr =
    side_effect_expr_statement_expressiont{std::move(block), type, location};
}

namespace
{
/// The defer statements and calls of recover of a function body (not of
/// the function literals in it).
struct defer_scant
{
  std::size_t defers = 0;
  bool defer_in_loop = false;
  bool has_goto = false;
  bool calls_recover = false;

  void scan(const irept &node, bool in_loop)
  {
    const irep_idt &kind = node.id();
    if(kind == "FuncLit")
      return;
    if(kind == "DeferStmt")
    {
      ++defers;
      defer_in_loop |= in_loop;
    }
    else if(kind == "BranchStmt" && node.get("Tok") == "goto")
      has_goto = true;
    else if(
      kind == "CallExpr" &&
      skip_instantiation(go_field(node, "Fun")).get("_obj") == "recover")
    {
      calls_recover = true;
    }
    const bool loop = in_loop || kind == "ForStmt" || kind == "RangeStmt";
    for(const auto &sub : node.get_sub())
      scan(sub, loop);
    for(const auto &named : node.get_named_sub())
      scan(named.second, loop || named.first == "Body");
  }
};
} // namespace

/// The type of deferred function values: func().
static struct_typet deferred_function_type()
{
  return go_type_convertert::func_value(code_typet{{}, empty_typet{}});
}

/// A call of the function value \p function of type func().
static codet
call_deferred(const exprt &function, const source_locationt &location)
{
  const auto &components = to_struct_type(function.type()).components();
  const member_exprt code{function, components[0]};
  const member_exprt env{function, components[1]};
  // Instructions generated for the call (by function-pointer removal) take
  // their location from the function expression.
  dereference_exprt callee{code};
  callee.add_source_location() = location;
  code_expressiont call{side_effect_expr_function_callt{
    std::move(callee), {env}, empty_typet{}, location}};
  call.add_source_location() = location;
  return std::move(call);
}

void go_convertert::begin_function_panics(
  const irept &body_node,
  code_blockt &dest)
{
  defer_scant scan;
  scan.scan(body_node, false);
  const source_locationt location = location_of(body_node);
  const auto local = [&](const typet &type, const exprt &value)
  {
    const symbol_exprt result = new_temporary(type, location, dest);
    code_frontend_assignt assign{result, value};
    assign.add_source_location() = location;
    dest.add(std::move(assign));
    return result;
  };

  if(uses_recover)
  {
    // Go1.27 §Handling_panics: "The return value of recover is nil when the
    // goroutine is not panicking or recover was not called directly by a
    // deferred function." The deferred calls set go::$defer_token, which
    // the first Go function they enter (the deferred function) takes.
    if(scan.calls_recover)
      state.may_recover = local(bool_typet{}, defer_token());
    code_frontend_assignt clear{defer_token(), false_exprt{}};
    clear.add_source_location() = location;
    dest.add(std::move(clear));
  }

  if(scan.defers == 0)
    return;
  state.has_defers = true;
  // A defer statement that may execute more than once (in a loop, or with
  // goto) needs a list of deferred calls; otherwise each has a variable.
  state.defer_list = scan.defer_in_loop || scan.has_goto;
  if(state.defer_list)
  {
    const pointer_typet list_type = go_type_convertert::env_pointer_type();
    state.defer_head = local(list_type, null_pointer_exprt{list_type});
  }
  else
  {
    const struct_typet function_type = deferred_function_type();
    for(std::size_t i = 0; i < scan.defers; ++i)
    {
      const exprt flag = local(bool_typet{}, false_exprt{});
      const exprt function =
        local(function_type, *zero_initializer(function_type, location, ns));
      state.defer_slots.emplace_back(flag, function);
    }
  }

  // Go1.27 §Defer_statements: "if the surrounding function returns through
  // an explicit return statement, deferred functions are executed after
  // any result parameters are set by that return statement but before the
  // function returns to its caller."
  if(state.return_type.id() != ID_empty && state.named_results.empty())
  {
    state.result_variable = local(
      state.return_type, *zero_initializer(state.return_type, location, ns));
  }
}

void go_convertert::end_function_panics(code_blockt &body)
{
  source_locationt location = body.source_location();
  location.set_function(state.function_id);
  body.add(code_labelt{UNWIND_LABEL, code_skipt{}});

  // Go1.27 §Defer_statements: "deferred functions are invoked immediately
  // before the surrounding function returns, in the reverse order they
  // were deferred."
  const auto run = [&](const exprt &function, code_blockt &dest)
  {
    if(uses_recover)
      dest.add(code_frontend_assignt{defer_token(), true_exprt{}});
    dest.add(call_deferred(function, location));
    if(uses_recover)
      dest.add(code_frontend_assignt{defer_token(), false_exprt{}});
  };
  if(state.has_defers && state.defer_list)
  {
    // while(head != NULL) { node = head; head = node->next; node->f(); }
    const struct_typet node_type = deferred_node_type();
    const pointer_typet node_pointer{node_type, config.ansi_c.pointer_width};
    code_blockt step;
    const symbol_exprt node = new_temporary(node_pointer, location, step);
    step.add(code_frontend_assignt{
      node, typecast_exprt{state.defer_head, node_pointer}});
    const dereference_exprt entry{node};
    step.add(code_frontend_assignt{
      state.defer_head, member_exprt{entry, node_type.components()[1]}});
    run(member_exprt{entry, node_type.components()[0]}, step);
    code_whilet loop{
      notequal_exprt{
        state.defer_head,
        null_pointer_exprt{to_pointer_type(state.defer_head.type())}},
      std::move(step)};
    loop.add_source_location() = location;
    body.add(std::move(loop));
  }
  for(auto slot = state.defer_slots.rbegin(); slot != state.defer_slots.rend();
      ++slot)
  {
    code_blockt call;
    run(slot->second, call);
    body.add(code_ifthenelset{slot->first, std::move(call)});
  }

  code_frontend_returnt ret;
  if(state.return_type.id() != ID_empty)
  {
    if(!state.has_defers)
    {
      // Only reached when panicking; the caller ignores the value.
      ret = code_frontend_returnt{
        side_effect_expr_nondett{state.return_type, location}};
    }
    else if(state.named_results.empty())
      ret = code_frontend_returnt{state.result_variable};
    else if(state.named_results.size() == 1)
      ret = code_frontend_returnt{state.named_results.front()};
    else
    {
      exprt::operandst values{
        state.named_results.begin(), state.named_results.end()};
      ret = code_frontend_returnt{
        struct_exprt{std::move(values), state.return_type}};
    }
  }
  ret.add_source_location() = location;
  body.add(std::move(ret));
}

void go_convertert::end_generated_function(code_blockt &body, const typet &type)
{
  // The code so far in a block of its own (see convert_function).
  code_blockt code;
  code.swap(body);
  body.add(std::move(code));
  body.add(code_labelt{UNWIND_LABEL, code_skipt{}});
  if(type.id() == ID_empty)
    body.add(code_frontend_returnt{});
  else
    body.add(code_frontend_returnt{
      side_effect_expr_nondett{type, source_locationt{}}});
}

struct_typet go_convertert::deferred_node_type()
{
  // The next node is a void pointer, to avoid a recursive type.
  return struct_typet{
    {{"f", deferred_function_type()},
     {"next", go_type_convertert::env_pointer_type()}}};
}

void go_convertert::convert_defer(const irept &stmt, code_blockt &dest)
{
  // Go1.27 §Defer_statements: "Each time a "defer" statement executes, the
  // function value and parameters to the call are evaluated as usual and
  // saved anew but the actual function is not invoked."
  const irept &call = go_field(stmt, "Call");
  const source_locationt location = location_of(stmt);
  // Go1.27 §Defer_statements: the deferred call belongs to the function
  // containing a range over a function, not to its yield function.
  if(state.range_body)
    return unsupported(stmt, "defer in the body of a range over a function");
  const irep_idt callee = callee_object(call);
  const irept &object = parse_tree.object(callee);
  if(object.get("kind") == "builtin")
  {
    if(callee == "recover")
    {
      // recover is then called by the deferral, not "directly by a
      // deferred function", so it returns nil and does not stop a panic.
      return;
    }
    return unsupported(call, "deferred calls of " + id2string(callee));
  }
  if(object.get("pkg") == "cprover")
    return unsupported(call, "deferred calls of " + id2string(callee));

  code_blockt prefix;
  const exprt deferred = saved_call(
    call,
    id2string(state.function_id) + "$defer" +
      std::to_string(state.defer_counter),
    location,
    prefix);
  if(deferred.is_nil())
    return;

  for(auto &statement : prefix.statements())
    dest.add(std::move(statement));
  if(state.defer_list)
  {
    const struct_typet node_type = deferred_node_type();
    const symbol_exprt node = new_heap_variable(
      node_type,
      struct_exprt{{deferred, state.defer_head}, node_type},
      location,
      dest);
    dest.add(code_frontend_assignt{
      state.defer_head, typecast_exprt{node, state.defer_head.type()}});
  }
  else
  {
    const auto &slot = state.defer_slots.at(state.defer_counter);
    dest.add(code_frontend_assignt{slot.second, deferred});
    dest.add(code_frontend_assignt{slot.first, true_exprt{}});
  }
  ++state.defer_counter;
}

exprt go_convertert::saved_call(
  const irept &call,
  const irep_idt &thunk_id,
  const source_locationt &location,
  code_blockt &prefix)
{
  // Function literals, declared functions and methods are not nil (a
  // method value of a nil interface value panics when it is evaluated).
  const irep_idt callee = callee_object(call);
  const irept &object = parse_tree.object(callee);
  const irept &fun = skip_instantiation(go_field(call, "Fun"));
  const bool may_be_nil =
    fun.id() != "FuncLit" &&
    !(fun.id() == "Ident" && object.get("kind") == "func") &&
    !(fun.id() == "SelectorExpr" && fun.find("_sel").is_not_nil() &&
      fun.find("_sel").get("kind") != "field");

  exprt function = convert_expr(go_field(call, "Fun"));
  if(function.is_nil())
    return nil_exprt{};
  PRECONDITION(is_func_value(function.type()));
  function = evaluate_once(function, location, prefix);
  code_typet code_type = to_code_type(
    to_pointer_type(to_struct_type(function.type()).components()[0].type())
      .base_type());
  code_type.parameters().erase(code_type.parameters().begin()); // $env
  exprt::operandst arguments;
  if(convert_arguments(call, code_type, arguments, prefix))
    return nil_exprt{};

  // The saved call: an environment with the function value and the
  // arguments, and a function calling it (named thunk_id).
  struct_typet::componentst components{{"f", function.type()}};
  exprt::operandst values{function};
  for(std::size_t i = 0; i < arguments.size(); ++i)
  {
    components.emplace_back(
      "a" + std::to_string(i), code_type.parameters()[i].type());
    values.push_back(arguments[i]);
  }
  const struct_typet env_type{std::move(components)};
  const symbol_exprt env = new_heap_variable(
    env_type, struct_exprt{std::move(values), env_type}, location, prefix);

  declare_generated(
    symbol_table,
    thunk_id,
    {go_type_convertert::env_pointer_type()},
    empty_typet{});
  {
    const symbolt &thunk = ns.lookup(thunk_id);
    const exprt env_param = parameter_symbols(thunk)[0];
    function_statet outer = std::move(state);
    state = function_statet{};
    state.function_id = thunk_id;
    code_blockt body;
    const dereference_exprt saved{typecast_exprt{
      env_param, pointer_typet{env_type, config.ansi_c.pointer_width}}};
    const member_exprt saved_function{saved, env_type.components()[0]};
    const auto &function_components =
      to_struct_type(saved_function.type()).components();
    const member_exprt code{saved_function, function_components[0]};
    // "If a deferred function value evaluates to nil, execution panics when
    // the function is invoked, not when the "defer" statement is
    // executed."
    if(may_be_nil)
    {
      add_panic(
        body,
        notequal_exprt{code, null_pointer_exprt{to_pointer_type(code.type())}},
        "nil-dereference",
        "runtime error: invalid memory address or nil pointer dereference",
        location);
    }
    exprt::operandst call_arguments{
      member_exprt{saved_function, function_components[1]}};
    for(std::size_t i = 1; i < env_type.components().size(); ++i)
      call_arguments.push_back(member_exprt{saved, env_type.components()[i]});
    // "If the deferred function has any return values, they are discarded
    // when the function completes."
    dereference_exprt callee{code};
    callee.add_source_location() = location;
    body.add(code_expressiont{side_effect_expr_function_callt{
      std::move(callee),
      std::move(call_arguments),
      code_type.return_type(),
      location}});
    end_generated_function(body, empty_typet{});
    state = std::move(outer);
    symbol_table.get_writeable_ref(thunk_id).value = std::move(body);
  }

  const struct_typet function_type = deferred_function_type();
  return struct_exprt{
    {typecast_exprt::conditional_cast(
       address_of_exprt{ns.lookup(thunk_id).symbol_expr()},
       function_type.components()[0].type()),
     typecast_exprt{env, function_type.components()[1].type()}},
    function_type};
}

void go_convertert::convert_go(const irept &stmt, code_blockt &dest)
{
  // Go1.27 §Go_statements: "The function value and parameters are evaluated
  // as usual in the calling goroutine, but unlike with a regular call,
  // program execution does not wait for the invoked function to complete.
  // Instead, the function begins executing independently in a new
  // goroutine." Goroutines are CBMC threads (a __CPROVER_ASYNC_ label, as
  // in the C library's pthread_create); the panic state is thread-local.
  const irept &call = go_field(stmt, "Call");
  const source_locationt location = location_of(stmt);
  const irep_idt callee = callee_object(call);
  const irept &object = parse_tree.object(callee);
  if(object.get("kind") == "builtin" || object.get("pkg") == "cprover")
    return unsupported(call, "go statements calling " + id2string(callee));

  code_blockt prefix;
  const irep_idt thunk_id =
    id2string(state.function_id) + "$go" + std::to_string(state.go_counter++);
  const exprt function = saved_call(call, thunk_id, location, prefix);
  if(function.is_nil())
    return;
  // The thread calls the generated function directly (rather than through
  // the function value), so that it is the only function called there.
  const exprt env = evaluate_once(function.operands()[1], location, prefix);
  for(auto &statement : prefix.statements())
    dest.add(std::move(statement));

  code_blockt thread;
  const auto assign = [&](const exprt &lhs, const exprt &rhs)
  {
    code_frontend_assignt result{lhs, rhs};
    result.add_source_location() = location;
    thread.add(std::move(result));
  };
  assign(panicking(), false_exprt{});
  assign(panic_site(), from_integer(0, panic_site().type()));
  if(uses_recover)
    assign(defer_token(), false_exprt{});
  // add_panic_checks leaves these calls alone: a panic must not unwind the
  // enclosing function, but reach the check after the goroutine's function.
  const symbolt &thunk = ns.lookup(thunk_id);
  side_effect_expr_function_callt thunk_call{
    thunk.symbol_expr(), {env}, empty_typet{}, location};
  thunk_call.set(ID_C_go_no_unwind, true);
  code_expressiont start_call{std::move(thunk_call)};
  start_call.add_source_location() = location;
  thread.add(std::move(start_call));
  // Go1.27 §Handling_panics: a panic that the goroutine does not recover
  // terminates the program.
  const symbolt &check = ns.lookup(check_panics_id);
  side_effect_expr_function_callt check_call{
    check.symbol_expr(), {}, empty_typet{}, location};
  check_call.set(ID_C_go_no_unwind, true);
  thread.add(code_expressiont{std::move(check_call)});
  code_labelt start{CPROVER_PREFIX "ASYNC_1", std::move(thread)};
  start.add_source_location() = location;
  dest.add(std::move(start));
}

exprt go_convertert::convert_recover(const irept &call)
{
  // Go1.27 §Handling_panics: "Suppose a function G defers a function D that
  // calls recover and a panic occurs in a function on the same goroutine
  // in which G is executing. When the running of deferred functions reaches
  // D, the return value of D's call to recover will be the value passed to
  // the call of panic. If D returns normally, without starting a new panic,
  // the panicking sequence stops." The panic stops when recover returns
  // it, so that D continues normally.
  const source_locationt location = location_of(call);
  const struct_typet type = go_type_convertert::interface_type();
  code_blockt block;
  const symbol_exprt result = new_temporary(type, location, block);
  block.add(
    code_frontend_assignt{result, *zero_initializer(type, location, ns)});
  if(state.may_recover.is_not_nil())
  {
    code_blockt recover;
    recover.add(code_frontend_assignt{result, panic_value()});
    recover.add(code_frontend_assignt{panicking(), false_exprt{}});
    block.add(code_ifthenelset{
      and_exprt{state.may_recover, panicking()}, std::move(recover)});
  }
  block.add(expression_statement(result, location));
  return side_effect_expr_statement_expressiont{
    std::move(block), type, location};
}

void go_convertert::finish_panics()
{
  // Go1.27 §Handling_panics: "At that point, the program is terminated and
  // the error condition is reported, including the value of the argument
  // to panic."
  const irep_idt id = check_panics_id;
  declare_generated(symbol_table, id, {}, empty_typet{});
  code_blockt body;
  for(std::size_t i = 0; i < panic_sites.size(); ++i)
  {
    code_assertt check{not_exprt{and_exprt{
      panicking(),
      equal_exprt{panic_site(), from_integer(i + 1, panic_site().type())}}}};
    check.add_source_location() = panic_sites[i];
    body.add(std::move(check));
  }
  symbol_table.get_writeable_ref(id).value = std::move(body);
}
