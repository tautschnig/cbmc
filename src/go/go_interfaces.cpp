/*******************************************************************\

Module: Go front-end: methods and interfaces

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// Conversion of methods (Go1.27 §Method_declarations, §Method_sets,
/// §Method_values, §Method_expressions) and interfaces (§Interface_types,
/// §Type_assertions, §Type_switches).
///
/// An interface value is `{type, data}`: the id of its dynamic type (0 for
/// nil; go_type_convertert::type_id) and a pointer to a copy of the dynamic
/// value on the heap. Interface values are never changed in place, so
/// copies may share the dynamic value. Calls of methods of interface
/// values, checks whether a dynamic type implements an interface, and
/// comparisons of interface values are generated functions that test the
/// dynamic type against every type that the program converts to an
/// interface; their bodies are generated after the whole program has been
/// converted (finish_interfaces).

#include <util/arith_tools.h>
#include <util/byte_operators.h>
#include <util/c_types.h>
#include <util/config.h>
#include <util/expr_initializer.h>
#include <util/pointer_expr.h>
#include <util/std_code.h>
#include <util/symbol_table_base.h>

#include "go_convert.h"
#include "go_parse_tree.h"

#include <set>

static member_exprt iface_field(const exprt &iface, const char *name)
{
  const struct_typet &type = to_struct_type(iface.type());
  return member_exprt{iface, type.get_component(name)};
}

std::size_t go_convertert::go_type_id(const typet &type) const
{
  const irep_idt &tid = type.get(ID_C_go_tid);
  return tid.empty() ? 0 : std::stoul(id2string(tid));
}

exprt go_convertert::assign_cast(exprt value, const typet &type)
{
  // A value whose conversion failed (and was reported) stays nil.
  if(value.is_nil())
    return value;
  // Go1.27 §Assignability: "V and T are ... T is an interface type ... and x
  // implements T": the value is stored in an interface value with its type
  // as dynamic type.
  if(is_go_interface(type) && !is_go_interface(value.type()))
  {
    if(value.id() == "go_nil")
      return go_cast(value, type, ns);
    return box(value, value.source_location());
  }
  return go_cast(std::move(value), type, ns);
}

exprt go_convertert::box(const exprt &value, const source_locationt &location)
{
  // The Go type is recorded on CBMC types (ID_C_go_tid); values built by
  // the converter may lack it, but then have a predeclared type or a
  // pointer to a recorded type.
  exprt boxed = value;
  std::size_t tid = go_type_id(value.type());
  if(tid == 0 && value.type().id() == ID_bool)
  {
    // Go1.27 §Comparison_operators: comparisons yield an untyped boolean,
    // whose default type is bool.
    boxed = typecast_exprt{value, convert_type_of_basic("bool")};
    tid = go_type_id(boxed.type());
  }
  else if(tid == 0 && value.type().id() == ID_c_bool)
    tid = type_converter.basic_type_id("bool");
  else if(tid == 0 && is_go_string(value.type()))
    tid = type_converter.basic_type_id("string");
  else if(tid == 0 && value.type().id() == ID_pointer)
  {
    tid = type_converter.pointer_type_id(
      go_type_id(to_pointer_type(value.type()).base_type()));
  }
  INVARIANT(tid != 0, "value converted to an interface has a Go type");
  dynamic_types.emplace(tid, boxed.type());
  const struct_typet type = go_type_convertert::interface_type();
  code_blockt block;
  const symbol_exprt copy =
    new_heap_variable(boxed.type(), boxed, location, block);
  const auto &components = type.components();
  return with_prefix(
    struct_exprt{
      {from_integer(tid, components[0].type()),
       typecast_exprt{copy, components[1].type()}},
      type},
    block,
    location);
}

typet go_convertert::convert_type_of_basic(const std::string &kind)
{
  return type_converter(
    type_converter.type_index(type_converter.basic_type_id(kind)));
}

exprt go_convertert::unbox(const exprt &iface, const typet &type)
{
  // Unboxing is only reached (or its value used) where the dynamic type of
  // iface is the type: its data then points to the start of an object of
  // that type (see box), and goto-symex may drop the other objects of the
  // pointer's value set, of the dynamic types of the other paths.
  dereference_exprt result{typecast_exprt{
    iface_field(iface, "data"),
    pointer_typet{type, config.ansi_c.pointer_width}}};
  result.set(ID_C_typed_dereference, true);
  return std::move(result);
}

void declare_generated(
  symbol_table_baset &symbol_table,
  const irep_idt &id,
  const std::vector<typet> &parameters,
  const typet &result)
{
  if(symbol_table.has_symbol(id))
    return;
  code_typet::parameterst params;
  for(std::size_t i = 0; i < parameters.size(); ++i)
  {
    code_typet::parametert param{parameters[i]};
    const irep_idt base = "$param" + std::to_string(i);
    param.set_identifier(id2string(id) + "::" + id2string(base));
    param.set_base_name(base);
    symbolt param_symbol{param.get_identifier(), parameters[i], "go"};
    param_symbol.base_name = base;
    param_symbol.is_parameter = true;
    param_symbol.is_lvalue = true;
    param_symbol.is_thread_local = true;
    param_symbol.is_file_local = true;
    symbol_table.add(param_symbol);
    params.push_back(std::move(param));
  }
  symbolt symbol{id, code_typet{std::move(params), result}, "go"};
  // A (non-nil) location, so that properties in the function keep their
  // property class and description (label_properties replaces nil ones).
  symbol.location = source_locationt{};
  symbol.location.set_function(id);
  const std::string name = id2string(id);
  symbol.base_name = name.substr(name.rfind('.') + 1);
  symbol.pretty_name = symbol.base_name;
  symbol_table.add(symbol);
}

std::vector<exprt> parameter_symbols(const symbolt &function)
{
  std::vector<exprt> result;
  for(const auto &p : to_code_type(function.type).parameters())
    result.push_back(symbol_exprt{p.get_identifier(), p.type()});
  return result;
}

irep_idt
go_convertert::dispatcher(const irep_idt &name, const irep_idt &signature_index)
{
  // One function per method name and signature: calls of that method of
  // any interface value.
  const std::size_t signature = type_converter.type_id(signature_index);
  const auto key = std::make_pair(name, signature);
  const auto it = dispatchers.find(key);
  if(it != dispatchers.end())
    return it->second;
  const irep_idt id =
    "go::$method." + std::to_string(signature) + "." + id2string(name);
  const code_typet code =
    to_code_type(type_converter.signature(signature_index));
  std::vector<typet> parameters{go_type_convertert::interface_type()};
  for(const auto &p : code.parameters())
    parameters.push_back(p.type());
  declare_generated(symbol_table, id, parameters, code.return_type());
  dispatchers.emplace(key, id);
  return id;
}

exprt go_convertert::implements(
  const exprt &type_id,
  const irep_idt &interface_index)
{
  // Go1.27 §Implementing_an_interface: "T is not an interface and is an
  // element of the type set of I"; for basic interfaces, T has all of I's
  // methods.
  const std::size_t tid = type_converter.type_id(interface_index);
  irep_idt &id = implements_checks[tid];
  if(id.empty())
  {
    id = "go::$implements." + std::to_string(tid);
    declare_generated(symbol_table, id, {signedbv_typet{32}}, c_bool_typet{8});
  }
  const symbolt &function = ns.lookup(id);
  return as_condition(side_effect_expr_function_callt{
    function.symbol_expr(),
    {type_id},
    to_code_type(function.type).return_type(),
    type_id.source_location()});
}

exprt go_convertert::interface_equal(
  const exprt &x,
  const exprt &y,
  const source_locationt &location)
{
  // Go1.27 §Comparison_operators: "Two interface values are equal if they
  // have identical dynamic types and equal dynamic values or if both have
  // value nil."
  const irep_idt id = "go::$interface_equal";
  declare_generated(
    symbol_table,
    id,
    {go_type_convertert::interface_type(),
     go_type_convertert::interface_type()},
    c_bool_typet{8});
  const symbolt &function = ns.lookup(id);
  return as_condition(side_effect_expr_function_callt{
    function.symbol_expr(),
    {x, y},
    to_code_type(function.type).return_type(),
    location});
}

exprt go_convertert::method_receiver(
  exprt x,
  const irept &path,
  const irep_idt &method_object,
  const source_locationt &location,
  code_blockt &checks)
{
  // Go1.27 §Selectors: "x.f denotes the field or method at the shallowest
  // depth in T", through embedded fields (path), dereferencing pointers on
  // the way. §Calls: "If x is addressable and &x's method set contains m,
  // x.m() is shorthand for (&x).m()"; a value receiver of a pointer x is
  // *x. The result is the receiver argument, or the interface value if the
  // method belongs to an interface type.
  const auto &steps = path.get_sub();
  for(std::size_t i = 0; i + 1 < steps.size(); ++i)
  {
    if(x.type().id() == ID_pointer)
    {
      x.add_source_location() = location;
      check_not_nil(x, checks);
      x = dereference_exprt{x};
    }
    const typet &followed = x.type().id() == ID_struct_tag
                              ? static_cast<const typet &>(
                                  ns.follow_tag(to_struct_tag_type(x.type())))
                              : x.type();
    const auto &components = to_struct_type(followed).components();
    x = member_exprt{x, components[std::stoul(steps[i].id_string())]};
  }
  if(is_go_interface(x.type()))
    return x;
  const symbolt *method_symbol = symbol_table.lookup(symbol_id(method_object));
  if(method_symbol == nullptr)
  {
    if(use_of_unsupported_declaration(method_object, location))
      return nil_exprt{};
    // A method of a standard-library type without a model.
    const irept &object = parse_tree.object(method_object);
    unsupported(
      location,
      "a call of " + id2string(method_object) + " (package " +
        object.get_string("pkg") + ", which has no model)");
    return nil_exprt{};
  }
  const symbolt &method = *method_symbol;
  const typet &receiver = to_code_type(method.type).parameters().front().type();
  if(receiver.id() == ID_pointer && x.type().id() != ID_pointer)
    return address_of_exprt{
      x, pointer_typet{x.type(), config.ansi_c.pointer_width}};
  if(receiver.id() != ID_pointer && x.type().id() == ID_pointer)
  {
    // Go1.27 §Method_values: "a reference to a non-interface method with a
    // value receiver using a pointer will automatically dereference that
    // pointer"; a nil pointer panics.
    x.add_source_location() = location;
    check_not_nil(x, checks);
    return dereference_exprt{x};
  }
  return x;
}

exprt go_convertert::call_method(
  const exprt &receiver,
  const irep_idt &method_object,
  const irep_idt &signature_index,
  exprt::operandst arguments,
  const source_locationt &location)
{
  // The receiver as from method_receiver; arguments already converted to
  // the parameter types.
  irep_idt function_id;
  if(is_go_interface(receiver.type()))
    function_id =
      dispatcher(parse_tree.object(method_object).get("name"), signature_index);
  else
    function_id = symbol_id(method_object);
  const symbolt &function = ns.lookup(function_id);
  const code_typet &code = to_code_type(function.type);
  arguments.insert(
    arguments.begin(),
    typecast_exprt::conditional_cast(receiver, code.parameters()[0].type()));
  return side_effect_expr_function_callt{
    function.symbol_expr(), std::move(arguments), code.return_type(), location};
}

exprt go_convertert::convert_method_call(const irept &call)
{
  // Go1.27 §Calls: "A method call x.m() is valid if the method set of (the
  // type of) x contains m and the argument list can be assigned to the
  // parameter list of m." The receiver is evaluated before the arguments.
  const source_locationt location = location_of(call);
  const irept *fun = &skip_instantiation(go_field(call, "Fun"));
  const irept &selection = fun->find("_sel");
  const irept &x_node = go_field(*fun, "X");
  const irep_idt &method_object = selection.get("obj");

  access_partst parts;
  const irep_idt &x_kind = type_converter.underlying(x_node.get("_t")).get("k");
  exprt x = x_kind == "pointer" || x_kind == "interface"
              ? access_operand(x_node, parts)
              : convert_access(x_node, parts);
  if(x.is_nil())
    return nil_exprt{};
  if(is_unmodelled_method(method_object))
    return unmodelled_call(call, method_object);
  const auto unsupported_method = unsupported_declarations.find(method_object);
  if(
    unsupported_method != unsupported_declarations.end() &&
    !is_go_interface(x.type()))
  {
    return unsupported_call(call, unsupported_method->second);
  }
  exprt receiver = method_receiver(
    x, selection.find("path"), method_object, location, parts.checks);
  if(receiver.is_nil())
    return nil_exprt{};
  code_blockt prefix;
  receiver = evaluate_once(with_parts(receiver, parts), location, prefix);

  // The parameters: those of the method's signature (the selector's type,
  // a method value without receiver).
  const typet signature = type_converter.signature(fun->get("_t"));
  if(signature.is_nil())
  {
    unsupported(call, type_converter.unsupported_reason);
    return nil_exprt{};
  }
  if(
    !is_go_interface(receiver.type()) &&
    !symbol_table.has_symbol(symbol_id(method_object)))
  {
    if(!use_of_unsupported_declaration(method_object, go_location(call)))
      unsupported(call, "calls of method " + id2string(method_object));
    return nil_exprt{};
  }
  exprt::operandst arguments;
  if(convert_arguments(call, to_code_type(signature), arguments, prefix))
    return nil_exprt{};
  exprt result = call_method(
    receiver, method_object, fun->get("_t"), std::move(arguments), location);
  return with_prefix(std::move(result), prefix, location);
}

exprt go_convertert::method_adapter(
  const irep_idt &id,
  const typet &receiver_type,
  const irep_idt &method_object,
  const irep_idt &signature_index,
  const source_locationt &location)
{
  // The function of a method value: the receiver, already adjusted by
  // method_receiver, is in the environment.
  const code_typet value_code =
    to_code_type(type_converter.signature(signature_index));
  if(!symbol_table.has_symbol(id))
  {
    std::vector<typet> parameters{go_type_convertert::env_pointer_type()};
    for(const auto &p : value_code.parameters())
      parameters.push_back(p.type());
    declare_generated(symbol_table, id, parameters, value_code.return_type());
    const symbolt &adapter = ns.lookup(id);
    const std::vector<exprt> params = parameter_symbols(adapter);
    const exprt receiver = dereference_exprt{typecast_exprt{
      params[0], pointer_typet{receiver_type, config.ansi_c.pointer_width}}};
    exprt::operandst arguments{params.begin() + 1, params.end()};
    exprt call = call_method(
      receiver, method_object, signature_index, arguments, location);
    code_blockt body;
    if(value_code.return_type().id() == ID_empty)
      body.add(code_expressiont{std::move(call)});
    else
      body.add(code_frontend_returnt{std::move(call)});
    end_generated_function(
      body, to_code_type(ns.lookup(id).type).return_type());
    symbol_table.get_writeable_ref(id).value = std::move(body);
  }
  return ns.lookup(id).symbol_expr();
}

exprt go_convertert::convert_method_value(const irept &selector)
{
  // Go1.27 §Method_values: "The expression x is evaluated and saved during
  // the evaluation of the method value; the saved copy is then used as the
  // receiver in any calls, which may be executed later." §Method_expressions
  // for selectors on types.
  const source_locationt location = location_of(selector);
  const irept &selection = selector.find("_sel");
  const irept &x_node = go_field(selector, "X");
  const irep_idt &method_object = selection.get("obj");
  const irep_idt &signature_index = selector.get("_t");
  const typet value_type = convert_type(signature_index, selector);
  if(value_type.is_nil())
    return nil_exprt{};
  const auto &components = to_struct_type(value_type).components();

  if(selection.get("kind") == "methodexpr")
  {
    // The adapter takes the receiver (of the type x denotes) first.
    const typet receiver_type = convert_type(x_node.get("_t"), x_node);
    if(receiver_type.is_nil())
      return nil_exprt{};
    const irep_idt id = id2string(symbol_id(method_object)) + "$expr" +
                        std::to_string(go_type_id(receiver_type));
    if(!symbol_table.has_symbol(id))
    {
      // The method expression's signature has the receiver first.
      const code_typet with_receiver =
        to_code_type(type_converter.signature(signature_index));
      std::vector<typet> parameters{go_type_convertert::env_pointer_type()};
      for(const auto &p : with_receiver.parameters())
        parameters.push_back(p.type());
      declare_generated(
        symbol_table, id, parameters, with_receiver.return_type());
      const symbolt &adapter = ns.lookup(id);
      const std::vector<exprt> params = parameter_symbols(adapter);
      code_blockt body;
      function_statet outer = std::move(state);
      state = function_statet{};
      state.function_id = id;
      exprt receiver = method_receiver(
        params[1], selection.find("path"), method_object, location, body);
      if(receiver.is_nil())
      {
        state = std::move(outer);
        return nil_exprt{};
      }
      exprt::operandst arguments{params.begin() + 2, params.end()};
      irep_idt method_signature = signature_index;
      if(is_go_interface(receiver.type()))
      {
        // The dispatcher is keyed by the method's own signature.
        method_signature = parse_tree.object(method_object).get("type");
      }
      exprt call = call_method(
        receiver, method_object, method_signature, arguments, location);
      state = std::move(outer);
      if(with_receiver.return_type().id() == ID_empty)
        body.add(code_expressiont{std::move(call)});
      else
        body.add(code_frontend_returnt{std::move(call)});
      end_generated_function(body, with_receiver.return_type());
      symbol_table.get_writeable_ref(id).value = std::move(body);
    }
    return struct_exprt{
      {typecast_exprt::conditional_cast(
         address_of_exprt{ns.lookup(id).symbol_expr()}, components[0].type()),
       null_pointer_exprt{to_pointer_type(components[1].type())}},
      value_type};
  }

  // A method value: evaluate the receiver now and keep a copy.
  access_partst parts;
  const irep_idt &x_kind = type_converter.underlying(x_node.get("_t")).get("k");
  exprt x = x_kind == "pointer" || x_kind == "interface"
              ? access_operand(x_node, parts)
              : convert_access(x_node, parts);
  if(x.is_nil())
    return nil_exprt{};
  exprt receiver = method_receiver(
    x, selection.find("path"), method_object, location, parts.checks);
  if(receiver.is_nil())
    return nil_exprt{};
  code_blockt prefix;
  receiver = evaluate_once(with_parts(receiver, parts), location, prefix);
  if(is_go_interface(receiver.type()))
  {
    // Go1.27 §Selectors: "If x is of interface type and has the value nil,
    // calling or evaluating the method x.f causes a run-time panic."
    const exprt type = iface_field(receiver, "type");
    add_panic(
      prefix,
      notequal_exprt{type, from_integer(0, type.type())},
      "nil-dereference",
      "runtime error: invalid memory address or nil pointer dereference",
      location);
  }
  if(
    !is_go_interface(receiver.type()) &&
    !symbol_table.has_symbol(symbol_id(method_object)))
  {
    if(!use_of_unsupported_declaration(method_object, go_location(selector)))
      unsupported(selector, "method " + id2string(method_object));
    return nil_exprt{};
  }
  const symbol_exprt env =
    new_heap_variable(receiver.type(), receiver, location, prefix);
  const irep_idt id = id2string(symbol_id(method_object)) + "$bound" +
                      std::to_string(go_type_id(receiver.type()));
  const exprt adapter = method_adapter(
    id, receiver.type(), method_object, signature_index, location);
  return with_prefix(
    struct_exprt{
      {typecast_exprt::conditional_cast(
         address_of_exprt{adapter}, components[0].type()),
       typecast_exprt{env, components[1].type()}},
      value_type},
    prefix,
    location);
}

exprt go_convertert::convert_type_assertion(const irept &expr)
{
  // Go1.27 §Type_assertions: "x.(T) asserts that x is not nil and that the
  // value stored in x is of type T." "if T is not an interface type,
  // x.(T) asserts that the dynamic type of x is identical to the type T." "If T
  // is an interface type, x.(T) asserts that the dynamic type of x
  // implements the interface T." "If the type assertion is false, a
  // run-time panic occurs." In the special form "v, ok = x.(T)", "The value
  // of ok is true if the assertion holds. Otherwise it is false and the
  // value of v is the zero value for type T. No run-time panic occurs in
  // this case."
  const source_locationt location = location_of(expr);
  const irept &type_node = go_field(expr, "Type");
  const bool comma_ok =
    type_converter.underlying(expr.get("_t")).get("k") == "tuple";
  const typet target = convert_type(type_node.get("_t"), type_node);
  exprt x = convert_expr(go_field(expr, "X"));
  if(target.is_nil() || x.is_nil())
    return nil_exprt{};
  code_blockt block;
  x = evaluate_once(x, location, block);
  const exprt holds = dynamic_type_is(x, type_node.get("_t"));
  const symbol_exprt ok = new_temporary(bool_typet{}, location, block);
  block.add(code_frontend_assignt{ok, holds});
  const exprt value =
    is_go_interface(target)
      ? exprt{if_exprt{ok, x, *zero_initializer(target, location, ns)}}
      : exprt{if_exprt{
          ok, unbox(x, target), *zero_initializer(target, location, ns)}};
  if(!comma_ok)
  {
    add_panic(
      block,
      ok,
      "type-assertion",
      "interface conversion: the type assertion does not hold",
      location);
    return with_prefix(value, block, location);
  }
  const typet tuple = expr_type(expr);
  const auto &components = to_struct_type(tuple).components();
  return with_prefix(
    struct_exprt{
      {go_cast(value, components[0].type(), ns),
       typecast_exprt::conditional_cast(ok, components[1].type())},
      tuple},
    block,
    location);
}

exprt go_convertert::dynamic_type_is(
  const exprt &iface,
  const irep_idt &type_index)
{
  // Whether the dynamic type of iface is the type at type_index or, for an
  // interface type, implements it; nil matches neither.
  const exprt type_id = iface_field(iface, "type");
  const irept &under = type_converter.underlying(type_index);
  if(under.get("k") == "interface")
  {
    return and_exprt{
      notequal_exprt{type_id, from_integer(0, type_id.type())},
      implements(type_id, type_index)};
  }
  return equal_exprt{
    type_id, from_integer(type_converter.type_id(type_index), type_id.type())};
}

void go_convertert::convert_type_switch(
  const irept &stmt,
  code_blockt &dest,
  const irep_idt &label_object)
{
  // Go1.27 §Type_switches: "Cases then match actual types T against the
  // dynamic type of the expression x." "Instead of a type, a case may use
  // the predeclared identifier nil; that case is selected when the
  // expression in the TypeSwitchGuard is a nil interface value." With the
  // short variable declaration, "the variable is declared at the end of the
  // TypeSwitchCase in the implicit block of each clause. In clauses with a
  // case listing exactly one type, the variable has that type; otherwise,
  // the variable has the type of the expression in the TypeSwitchGuard."
  // The spec's rewriting into if statements evaluates x "exactly once" and
  // tests the cases in order.
  const source_locationt location = location_of(stmt);
  code_blockt scope;
  const irept &init = go_field(stmt, "Init");
  if(init.is_not_nil())
    convert_statement(init, scope);
  const irept &guard = go_field(stmt, "Assign");
  const irept &assertion = guard.id() == "AssignStmt"
                             ? go_list(guard, "Rhs").front()
                             : go_field(guard, "X");
  exprt x = convert_expr(go_field(assertion, "X"));
  if(x.is_nil())
    return;
  const symbol_exprt value = new_temporary(x.type(), location, scope);
  scope.add(code_frontend_assignt{value, x});

  const std::string prefix =
    "$switch" + std::to_string(state.label_counter++) + "$";
  const irep_idt end_label = prefix + "end";
  const auto &clauses = go_list(go_field(stmt, "Body"), "List");
  irep_idt default_label = end_label;
  for(std::size_t i = 0; i < clauses.size(); ++i)
  {
    const auto &cases = go_list(clauses[i], "List");
    if(cases.empty())
      default_label = prefix + std::to_string(i);
    for(const auto &case_node : cases)
    {
      const bool is_nil =
        !case_node.get_bool("_tx") &&
        parse_tree.object(case_node.get("_obj")).get("kind") == "nil";
      const exprt matches =
        is_nil ? exprt{equal_exprt{
                   iface_field(value, "type"),
                   from_integer(0, iface_field(value, "type").type())}}
               : dynamic_type_is(value, case_node.get("_t"));
      code_ifthenelset dispatch{
        matches, code_gotot{prefix + std::to_string(i)}};
      dispatch.add_source_location() = location_of(case_node);
      scope.add(std::move(dispatch));
    }
  }
  scope.add(code_gotot{default_label});

  state.jump_targets.push_back({label_object, false, end_label, irep_idt{}});
  for(std::size_t i = 0; i < clauses.size(); ++i)
  {
    const irept &clause = clauses[i];
    code_blockt block;
    block.add_source_location() = location_of(clause);
    const irep_idt &implicit = clause.get("_impl");
    if(!implicit.empty())
    {
      const irept &object = parse_tree.object(implicit);
      const typet type = convert_type(object.get("type"), clause);
      if(type.is_nil())
        continue;
      const exprt var =
        declare_local(implicit, type, location_of(clause), block);
      const exprt init_value =
        is_go_interface(type) ? exprt{value} : unbox(value, type);
      block.add(code_frontend_assignt{var, init_value});
    }
    for(const auto &s : go_list(clause, "Body"))
      convert_statement(s, block);
    code_labelt labelled{prefix + std::to_string(i), std::move(block)};
    labelled.add_source_location() = location_of(clause);
    scope.add(std::move(labelled));
    scope.add(code_gotot{end_label});
  }
  state.jump_targets.pop_back();
  scope.add(code_labelt{end_label, code_skipt{}});
  dest.add(std::move(scope));
}

bool go_convertert::is_comparable(const typet &type) const
{
  // Go1.27 §Comparison_operators: "Slice, map, and function types are not
  // comparable." Structs and arrays are comparable if their fields and
  // elements are.
  const typet &followed =
    type.id() == ID_struct_tag
      ? static_cast<const typet &>(ns.follow_tag(to_struct_tag_type(type)))
      : type;
  if(is_go_slice(followed) || is_go_map(followed) || is_func_value(followed))
    return false;
  if(is_go_string(followed) || is_go_interface(followed))
    return true;
  if(followed.id() == ID_array)
    return is_comparable(to_array_type(followed).element_type());
  if(followed.id() == ID_struct)
  {
    for(const auto &component : to_struct_type(followed).components())
      if(!is_comparable(component.type()))
        return false;
  }
  return true;
}

void go_convertert::finish_interfaces()
{
  // The generated functions test the dynamic type against every type that
  // the program converts to an interface (dynamic_types); no other type
  // can be the dynamic type of an interface value.
  const source_locationt location;
  const auto type_test = [&](const exprt &iface, std::size_t tid)
  {
    const exprt type_id = iface_field(iface, "type");
    return equal_exprt{type_id, from_integer(tid, type_id.type())};
  };
  const auto method_set_entry = [&](
                                  std::size_t tid,
                                  const irep_idt &name,
                                  std::size_t signature) -> const irept *
  {
    const irept &entry = parse_tree.type_entry(type_converter.type_index(tid));
    for(const auto &m : go_list(entry, "mset"))
    {
      if(
        m.get("name") == name &&
        type_converter.type_id(m.get("type")) == signature)
      {
        return &m;
      }
    }
    return nullptr;
  };

  // Method calls on interface values. Dispatchers may be added while
  // others are generated (methods promoted from embedded interfaces).
  std::set<irep_idt> done;
  for(bool more = true; more;)
  {
    more = false;
    const auto pending = dispatchers;
    for(const auto &d : pending)
    {
      if(!done.insert(d.second).second)
        continue;
      more = true;
      const symbolt &function = ns.lookup(d.second);
      const std::vector<exprt> params = parameter_symbols(function);
      const exprt &iface = params[0];
      const typet result_type = to_code_type(function.type).return_type();
      code_blockt body;
      function_statet outer = std::move(state);
      state = function_statet{};
      state.function_id = d.second;
      for(const auto &t : dynamic_types)
      {
        const irept *entry =
          method_set_entry(t.first, d.first.first, d.first.second);
        if(entry == nullptr)
          continue;
        code_blockt call_block;
        // A method without a model fails a property when called (see
        // convert_function_body).
        std::vector<std::string> messages;
        unsupported_in_body = &messages;
        const exprt receiver = method_receiver(
          unbox(iface, t.second),
          entry->find("path"),
          entry->get("obj"),
          location,
          call_block);
        unsupported_in_body = nullptr;
        if(receiver.is_nil())
        {
          if(!messages.empty())
          {
            body.add(code_ifthenelset{
              type_test(iface, t.first),
              unsupported_body(function.location, messages.front())});
          }
          continue;
        }
        exprt::operandst arguments{params.begin() + 1, params.end()};
        exprt call = call_method(
          receiver,
          entry->get("obj"),
          entry->get("type"),
          std::move(arguments),
          location);
        if(result_type.id() == ID_empty)
        {
          call_block.add(code_expressiont{std::move(call)});
          call_block.add(code_frontend_returnt{});
        }
        else
        {
          call_block.add(code_frontend_returnt{
            typecast_exprt::conditional_cast(call, result_type)});
        }
        body.add(
          code_ifthenelset{type_test(iface, t.first), std::move(call_block)});
      }
      // Go1.27 §Run_time_panics: calling a method of a nil interface value
      // panics; other dynamic types cannot occur.
      add_panic(
        body,
        false_exprt{},
        "nil-dereference",
        "runtime error: invalid memory address or nil pointer dereference",
        function.location);
      end_generated_function(body, result_type);
      state = std::move(outer);
      symbol_table.get_writeable_ref(d.second).value = std::move(body);
    }
  }

  // Implements checks.
  for(const auto &c : implements_checks)
  {
    const symbolt &function = ns.lookup(c.second);
    const exprt type_id = parameter_symbols(function)[0];
    const irept &iface_entry =
      type_converter.underlying(type_converter.type_index(c.first));
    exprt::operandst cases;
    for(const auto &t : dynamic_types)
    {
      bool all = true;
      for(const auto &m : go_list(iface_entry, "methods"))
      {
        if(
          method_set_entry(
            t.first, m.get("name"), type_converter.type_id(m.get("type"))) ==
          nullptr)
        {
          all = false;
          break;
        }
      }
      if(all)
        cases.push_back(
          equal_exprt{type_id, from_integer(t.first, type_id.type())});
    }
    code_blockt body;
    body.add(code_frontend_returnt{
      typecast_exprt{disjunction(std::move(cases)), c_bool_typet{8}}});
    symbol_table.get_writeable_ref(c.second).value = std::move(body);
  }

  // Interface comparison.
  if(symbol_table.has_symbol("go::$interface_equal"))
  {
    const symbolt &function = ns.lookup("go::$interface_equal");
    const std::vector<exprt> params = parameter_symbols(function);
    code_blockt body;
    function_statet outer = std::move(state);
    state = function_statet{};
    state.function_id = function.name;
    const exprt a_type = iface_field(params[0], "type");
    const exprt b_type = iface_field(params[1], "type");
    const c_bool_typet result_type{8};
    body.add(code_ifthenelset{
      notequal_exprt{a_type, b_type},
      code_frontend_returnt{from_integer(0, result_type)}});
    body.add(code_ifthenelset{
      equal_exprt{a_type, from_integer(0, a_type.type())},
      code_frontend_returnt{from_integer(1, result_type)}});
    for(const auto &t : dynamic_types)
    {
      code_blockt compare;
      if(is_comparable(t.second))
      {
        compare.add(code_frontend_returnt{typecast_exprt{
          as_condition(equality(
            unbox(params[0], t.second), unbox(params[1], t.second), irept{})),
          result_type}});
      }
      else
      {
        // "A comparison of two interface values with identical dynamic
        // types causes a run-time panic if that type is not comparable."
        add_panic(
          compare,
          false_exprt{},
          "uncomparable",
          "runtime error: comparing uncomparable type " +
            t.second.get_string(ID_C_go_type),
          function.location);
      }
      body.add(
        code_ifthenelset{type_test(params[0], t.first), std::move(compare)});
    }
    body.add(code_frontend_returnt{from_integer(0, result_type)});
    state = std::move(outer);
    end_generated_function(body, result_type);
    symbol_table.get_writeable_ref(function.name).value = std::move(body);
  }
}

bool go_convertert::is_intrinsic(const irep_idt &object_id)
{
  static const std::set<irep_idt> intrinsics{
    "cprover/rt.Comparable",
    "cprover/rt.SameType",
    "cprover/rt.SliceLen",
    "cprover/rt.SliceSwap",
    "cprover/rt.Float64bits",
    "cprover/rt.Float64frombits",
    "cprover/rt.Float32bits",
    "cprover/rt.Float32frombits",
    "cprover/rt.AsTarget",
    "cprover/rt.AsAssign",
    // models/reflect (see go_reflect.cpp)
    "reflect.typeOf",
    "reflect.ifaceType",
    "reflect.ifaceData",
    "reflect.makeIface",
    "reflect.box",
    "reflect.assign",
    "reflect.alloc",
    "reflect.equal",
    "reflect.mapFind",
    "reflect.mapStore",
    "reflect.mapDelete",
    "reflect.makeMap",
    "reflect.makeSlice",
    "reflect.sliceHeader",
    "reflect.setSlice",
    "reflect.mapHeader",
    "reflect.chanLenCap",
    "reflect.pointerAt",
    "reflect.setPointer",
    "reflect.sliceOf",
    "reflect.mapOf",
    "reflect.arrayOf",
    "reflect.chanOf"};
  return intrinsics.count(object_id) != 0;
}

void go_convertert::finish_intrinsics()
{
  const auto body_of = [&](const irep_idt &object_id) -> symbolt *
  {
    symbolt *function = symbol_table.get_writeable(symbol_id(object_id));
    return function != nullptr && function->value.is_nil() ? function : nullptr;
  };

  // Comparable: Go1.27 §Comparison_operators, for each dynamic type.
  if(symbolt *function = body_of("cprover/rt.Comparable"))
  {
    const exprt x = parameter_symbols(*function)[0];
    const exprt type = iface_field(x, "type");
    exprt::operandst uncomparable;
    for(const auto &t : dynamic_types)
    {
      if(!is_comparable(t.second))
        uncomparable.push_back(
          equal_exprt{type, from_integer(t.first, type.type())});
    }
    const typet &result = to_code_type(function->type).return_type();
    code_blockt body{{code_frontend_returnt{typecast_exprt{
      not_exprt{disjunction(std::move(uncomparable))}, result}}}};
    function->value = std::move(body);
  }

  // SliceLen and SliceSwap: on the slice of each dynamic slice type (as
  // package internal/reflectlite does with ValueOf(x).Len() and Swapper).
  for(const char *name : {"cprover/rt.SliceLen", "cprover/rt.SliceSwap"})
  {
    symbolt *function = body_of(name);
    if(function == nullptr)
      continue;
    const bool is_len = std::string{name} == "cprover/rt.SliceLen";
    const std::vector<exprt> params = parameter_symbols(*function);
    const typet result = to_code_type(function->type).return_type();
    function_statet outer = std::move(state);
    state = function_statet{};
    state.function_id = function->name;
    const source_locationt location = function->location;
    const exprt type = iface_field(params[0], "type");
    code_blockt body;
    for(const auto &t : dynamic_types)
    {
      if(!is_go_slice(t.second))
        continue;
      const exprt slice = unbox(params[0], t.second);
      const member_exprt data{
        slice, to_struct_type(t.second).get_component("data")};
      const member_exprt length{
        slice, to_struct_type(t.second).get_component("len")};
      code_blockt branch;
      if(is_len)
        branch.add(code_frontend_returnt{
          typecast_exprt::conditional_cast(length, result)});
      else
      {
        const exprt &i = params[1];
        const exprt &j = params[2];
        const auto in_range = [&](const exprt &k)
        {
          const exprt wide = typecast_exprt::conditional_cast(k, length.type());
          return and_exprt{
            binary_relation_exprt{wide, ID_ge, from_integer(0, length.type())},
            binary_relation_exprt{wide, ID_lt, length}};
        };
        add_panic(
          branch,
          and_exprt{in_range(i), in_range(j)},
          "index-out-of-range",
          "runtime error: index out of range",
          location);
        const typet &element = to_pointer_type(data.type()).base_type();
        const dereference_exprt at_i{plus_exprt{data, i}};
        const dereference_exprt at_j{plus_exprt{data, j}};
        const symbol_exprt tmp = new_temporary(element, location, branch);
        branch.add(code_frontend_assignt{tmp, at_i});
        branch.add(code_frontend_assignt{at_i, at_j});
        branch.add(code_frontend_assignt{at_j, tmp});
        branch.add(code_frontend_returnt{});
      }
      body.add(code_ifthenelset{
        equal_exprt{type, from_integer(t.first, type.type())},
        std::move(branch)});
    }
    // reflectlite panics with a *ValueError (here its message).
    add_panic(
      body,
      false_exprt{},
      "panic",
      is_len ? "reflect: call of reflect.Value.Len on a value that is not "
               "a slice"
             : "reflect: call of Swapper on a value that is not a slice",
      location);
    end_generated_function(body, result);
    state = std::move(outer);
    function->value = std::move(body);
  }

  // SameType: the dynamic type ids are equal (0 for nil).
  if(symbolt *function = body_of("cprover/rt.SameType"))
  {
    const std::vector<exprt> params = parameter_symbols(*function);
    const typet &result = to_code_type(function->type).return_type();
    code_blockt body{{code_frontend_returnt{typecast_exprt{
      equal_exprt{
        iface_field(params[0], "type"), iface_field(params[1], "type")},
      result}}}};
    function->value = std::move(body);
  }

  // The IEEE 754 representations (Go1.27 §Numeric_types: float32 and
  // float64 are IEEE 754 binary32 and binary64): the same bits.
  for(const char *name :
      {"cprover/rt.Float64bits",
       "cprover/rt.Float64frombits",
       "cprover/rt.Float32bits",
       "cprover/rt.Float32frombits"})
  {
    if(symbolt *function = body_of(name))
    {
      const exprt x = parameter_symbols(*function)[0];
      const typet &result = to_code_type(function->type).return_type();
      code_blockt body{{code_frontend_returnt{
        make_byte_extract(x, from_integer(0, c_index_type()), result)}}};
      function->value = std::move(body);
    }
  }
}

void go_convertert::finish_errors_as()
{
  // AsTarget(target any) int and AsAssign(target any, err error) bool, for
  // errors.As (models/errors), over the dynamic pointer types *T: what
  // reflectlite does there.
  symbolt *target_check = symbol_table.get_writeable("go::cprover/rt.AsTarget");
  symbolt *assign = symbol_table.get_writeable("go::cprover/rt.AsAssign");
  if(target_check != nullptr && target_check->value.is_not_nil())
    target_check = nullptr;
  if(assign != nullptr && assign->value.is_not_nil())
    assign = nullptr;
  if(target_check == nullptr && assign == nullptr)
    return;

  // Whether the type with id tid has method Error() string (Go1.27
  // §Errors) in its method set.
  const auto has_error_method = [&](std::size_t tid)
  {
    const irept &entry = parse_tree.type_entry(type_converter.type_index(tid));
    for(const auto &m : go_list(entry, "mset"))
    {
      if(m.get("name") != "Error")
        continue;
      const irept &signature = type_converter.underlying(m.get("type"));
      const auto &results = go_list(signature, "results");
      if(
        go_list(signature, "params").empty() && results.size() == 1 &&
        type_converter.basic_kind(results.front().get("type")) == "string")
      {
        return true;
      }
    }
    return false;
  };
  // The dynamic types that are pointers *T (not maps, channels or
  // unsafe.Pointer), with the type id of T.
  std::vector<std::pair<std::size_t, std::size_t>> pointers;
  for(const auto &t : dynamic_types)
  {
    if(
      t.second.id() != ID_pointer || is_go_map(t.second) ||
      is_go_chan(t.second))
      continue;
    const std::size_t base = go_type_id(to_pointer_type(t.second).base_type());
    if(base != 0)
      pointers.emplace_back(t.first, base);
  }
  if(target_check != nullptr)
  {
    // 0: a valid target; 1: not a non-nil pointer; 2: *target is neither
    // an interface nor implements error.
    const exprt target = parameter_symbols(*target_check)[0];
    const typet result = to_code_type(target_check->type).return_type();
    const exprt type = iface_field(target, "type");
    code_blockt body;
    for(const auto &p : pointers)
    {
      const typet &pointer = dynamic_types.at(p.first);
      const bool is_interface =
        type_converter.underlying(type_converter.type_index(p.second))
          .get("k") == "interface";
      const bool valid = is_interface || has_error_method(p.second);
      const exprt value = unbox(target, pointer);
      body.add(code_ifthenelset{
        equal_exprt{type, from_integer(p.first, type.type())},
        code_frontend_returnt{if_exprt{
          equal_exprt{value, null_pointer_exprt{to_pointer_type(pointer)}},
          from_integer(1, result),
          from_integer(valid ? 0 : 2, result)}}});
    }
    body.add(code_frontend_returnt{from_integer(1, result)});
    target_check->value = std::move(body);
  }

  if(assign != nullptr)
  {
    // If err's dynamic type is assignable to T (Go1.27 §Assignability:
    // identical, or implementing interface T), *target = err.
    const std::vector<exprt> params = parameter_symbols(*assign);
    const exprt &target = params[0];
    const exprt &err = params[1];
    const typet result = to_code_type(assign->type).return_type();
    const exprt type = iface_field(target, "type");
    const exprt err_type = iface_field(err, "type");
    function_statet outer = std::move(state);
    state = function_statet{};
    state.function_id = assign->name;
    code_blockt body;
    for(const auto &p : pointers)
    {
      const typet &pointer = dynamic_types.at(p.first);
      const typet &base = to_pointer_type(pointer).base_type();
      const irep_idt base_index = type_converter.type_index(p.second);
      const bool is_interface =
        type_converter.underlying(base_index).get("k") == "interface";
      const dereference_exprt destination{unbox(target, pointer)};
      code_blockt branch;
      code_blockt store;
      exprt condition;
      if(is_interface)
      {
        condition = and_exprt{
          notequal_exprt{err_type, from_integer(0, err_type.type())},
          implements(err_type, base_index)};
        store.add(code_frontend_assignt{
          destination, typecast_exprt::conditional_cast(err, base)});
      }
      else
      {
        condition =
          equal_exprt{err_type, from_integer(p.second, err_type.type())};
        store.add(code_frontend_assignt{destination, unbox(err, base)});
      }
      store.add(code_frontend_returnt{from_integer(1, result)});
      branch.add(code_ifthenelset{condition, std::move(store)});
      branch.add(code_frontend_returnt{from_integer(0, result)});
      body.add(code_ifthenelset{
        equal_exprt{type, from_integer(p.first, type.type())},
        std::move(branch)});
    }
    body.add(code_frontend_returnt{from_integer(0, result)});
    end_generated_function(body, result);
    state = std::move(outer);
    assign->value = std::move(body);
  }
}
