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
#include <util/config.h>
#include <util/cprover_prefix.h>
#include <util/expr_initializer.h>
#include <util/expr_util.h>
#include <util/mp_arith.h>
#include <util/pointer_expr.h>
#include <util/pointer_offset_size.h>
#include <util/prefix.h>
#include <util/std_code.h>
#include <util/symbol_table_base.h>

#include "go_parse_tree.h"

#include <set>

go_convertert::go_convertert(
  symbol_table_baset &symbol_table,
  const go_parse_treet &parse_tree,
  const go_convert_optionst &options,
  message_handlert &message_handler)
  : symbol_table(symbol_table),
    parse_tree(parse_tree),
    append_capacity(options.append_capacity),
    nondet_parameters(options.nondet),
    entry_function(options.entry_function),
    deadlock_check(options.deadlock_check),
    race_check(options.race_check),
    log(message_handler),
    ns(symbol_table),
    type_converter(parse_tree, symbol_table)
{
}

/// Id of the expression standing for the untyped nil until its context
/// determines the type (see go_convertert::convert_expr).
static const irep_idt ID_go_nil = "go_nil";

/// True for the packages whose declarations are converted: the program's
/// and the runtime package cprover/rt (helper/runtime/rt.go).
bool go_convertert::is_converted_package(const irep_idt &package) const
{
  return converted_packages.count(package) != 0;
}

/// \p expr with location \p location.
static exprt at_location(exprt expr, const source_locationt &location)
{
  expr.add_source_location() = location;
  return expr;
}

/// An expression statement evaluating \p value, at \p location.
static code_expressiont
expression_statement(exprt value, const source_locationt &location)
{
  code_expressiont result{std::move(value)};
  result.add_source_location() = location;
  return result;
}

const typet &go_follow(const namespacet &ns, const typet &type)
{
  if(type.id() == ID_struct_tag)
    return ns.follow_tag(to_struct_tag_type(type));
  return type;
}

/// True if the Go AST \p node contains a call.
static bool contains_call(const irept &node)
{
  if(node.id() == "CallExpr")
    return true;
  for(const auto &sub : node.get_sub())
    if(contains_call(sub))
      return true;
  for(const auto &named : node.get_named_sub())
    if(contains_call(named.second))
      return true;
  return false;
}

bool contains_reference(
  const typet &type,
  const namespacet &ns,
  bool strings_too)
{
  const typet &followed = go_follow(ns, type);
  if(followed.id() == ID_pointer || is_func_value(followed))
    return true;
  if(is_go_string(followed))
    return strings_too;
  if(is_go_slice(followed))
  {
    // An arbitrary slice gets a new underlying array of arbitrary
    // elements, which must not contain references themselves.
    return strings_too ||
           contains_reference(
             to_pointer_type(
               to_struct_type(followed).get_component("data").type())
               .base_type(),
             ns,
             true);
  }
  if(followed.id() == ID_array)
    return contains_reference(to_array_type(followed).element_type(), ns, true);
  if(followed.id() == ID_struct)
  {
    for(const auto &component : to_struct_type(followed).components())
      if(contains_reference(component.type(), ns, strings_too))
        return true;
  }
  return false;
}

/// \p value, or the nil of \p type if \p value is the untyped nil.
static exprt
go_cast_nil(const exprt &value, const typet &type, const namespacet &ns);

static exprt
convert_composite(const exprt &value, const typet &type, const namespacet &ns);

exprt as_condition(exprt value)
{
  if(value.type().id() == ID_bool)
    return value;
  source_locationt location = value.source_location();
  typecast_exprt result{std::move(value), bool_typet{}};
  result.add_source_location() = std::move(location);
  return std::move(result);
}

exprt go_cast(exprt value, const typet &type, const namespacet &ns)
{
  // Function types that refer to themselves are struct tags (see
  // go_type_convertert::named_signature).
  const typet &followed = go_follow(ns, type);
  if(value.id() == ID_go_nil)
  {
    source_locationt location = value.source_location();
    exprt result;
    if(type.id() == ID_pointer)
      result = null_pointer_exprt{to_pointer_type(type)};
    else if(is_go_interface(type))
    {
      // Go1.27 §Interface_types: "The value of an uninitialized variable of
      // interface type is nil": no dynamic type.
      const auto &components = to_struct_type(type).components();
      result = struct_exprt{
        {from_integer(0, components[0].type()),
         null_pointer_exprt{to_pointer_type(components[1].type())}},
        type};
    }
    else if(is_go_slice(type))
    {
      // Go1.27 §Slice_types: "The value of an uninitialized slice is nil."
      const auto &components = to_struct_type(type).components();
      result = struct_exprt{
        {null_pointer_exprt{to_pointer_type(components[0].type())},
         from_integer(0, components[1].type()),
         from_integer(0, components[2].type())},
        type};
    }
    else
    {
      // A nil function value.
      PRECONDITION(is_func_value(followed));
      const auto &components = to_struct_type(followed).components();
      result = struct_exprt{
        {null_pointer_exprt{to_pointer_type(components[0].type())},
         null_pointer_exprt{to_pointer_type(components[1].type())}},
        type};
    }
    result.add_source_location() = std::move(location);
    return result;
  }
  if(
    value.type() != type && followed.id() == ID_struct &&
    (type.id() == ID_struct_tag || value.type().id() == ID_struct_tag))
  {
    // A struct and the tag naming it: rebuild the struct.
    exprt result = convert_composite(value, type, ns);
    if(result.is_not_nil())
      return result;
  }
  return typecast_exprt::conditional_cast(std::move(value), type);
}

static exprt
go_cast_nil(const exprt &value, const typet &type, const namespacet &ns)
{
  return value.id() == ID_go_nil ? go_cast(value, type, ns) : value;
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
  if(unsupported_in_body != nullptr && !unsupported_in_body->empty())
  {
    // A consequence of the unsupported construct already reported, in a
    // body that will not be used.
    log.debug().source_location = go_location(node);
    log.debug() << message << messaget::eom;
    return;
  }
  log.error().source_location = go_location(node);
  log.error() << message << messaget::eom;
  error_found = true;
}

void go_convertert::unsupported(const irept &node, const std::string &what)
{
  unsupported(go_location(node), what);
}

void go_convertert::unsupported(
  const source_locationt &location,
  const std::string &what)
{
  const std::string message = "Go front-end: " + what + " is not supported yet";
  if(unsupported_in_body != nullptr)
  {
    // Later ones are often consequences of the first (such as uses of a
    // variable whose declaration was not supported).
    auto &stream = unsupported_in_body->empty() ? log.warning() : log.debug();
    stream.source_location = location;
    stream << message << messaget::eom;
    unsupported_in_body->push_back(message);
    return;
  }
  log.error().source_location = location;
  log.error() << message << messaget::eom;
  error_found = true;
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
  typet type = type_converter(object.get("type"));
  if(type.is_nil())
  {
    // Not an error: uses of the variable, and its initialisation, fail a
    // property of class "unsupported" (see variable and
    // convert_package_init), so programs that do not use it are not
    // affected.
    unsupported_declaration(
      object_id,
      where,
      "package variable '" + object.get_string("name") + "' of type " +
        parse_tree.type_entry(object.get("type")).get_string("str") + " (" +
        type_converter.unsupported_reason + ")");
    return;
  }
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

void go_convertert::unsupported_declaration(
  const irep_idt &object_id,
  const irept &where,
  const std::string &what)
{
  // Not an error: uses of the object fail a property of class
  // "unsupported" (see use_of_unsupported_declaration), so programs that
  // do not use it are not affected.
  unsupported_declarations[object_id] = what;
  log.warning().source_location = location_of(where);
  log.warning() << "Go front-end: " << what << " is not supported yet"
                << messaget::eom;
}

bool go_convertert::use_of_unsupported_declaration(
  const irep_idt &object_id,
  const source_locationt &location)
{
  const auto it = unsupported_declarations.find(object_id);
  if(it == unsupported_declarations.end())
    return false;
  unsupported(location, it->second);
  return true;
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
  // Functions without a body (implemented in assembly or by the runtime)
  // are declared; calls of them fail (see convert_function_body).
  const irept &name = go_field(decl, "Name");
  const irep_idt &object_id = name.get("_obj");
  const irept &object = parse_tree.object(object_id);
  if(go_field(go_field(decl, "Type"), "TypeParams").is_not_nil())
    return unsupported(decl, "generic functions");

  typet type = type_converter.signature(object.get("type"));
  const irept &signature = parse_tree.type_entry(object.get("type"));
  const auto unsupported_signature = [&]()
  {
    unsupported_declaration(
      object_id,
      decl,
      "function " + id2string(object_id) + " of type " +
        signature.get_string("str") + " (" + type_converter.unsupported_reason +
        ")");
  };
  if(type.is_nil())
    return unsupported_signature();
  code_typet &code_type = to_code_type(type);
  declare_parameters(code_type, signature, object_id, location_of(decl));

  const irept &receiver = signature.find("recv");
  if(receiver.is_not_nil())
  {
    // Go1.27 §Method_declarations: "A method is a function with a
    // receiver." The receiver is the first parameter.
    if(!go_list(go_field(decl, "Type"), "TypeParams").empty())
      return unsupported(decl, "generic methods");
    typet receiver_type = type_converter(receiver.get("type"));
    if(receiver_type.is_nil())
      return unsupported_signature();
    irep_idt receiver_object = receiver.get("obj");
    irep_idt base = receiver.get("name");
    if(receiver_object.empty() || base == "_")
    {
      receiver_object = id2string(object_id) + "::$recv";
      base = "$recv";
    }
    code_typet::parametert param{receiver_type};
    param.set_identifier(symbol_id(receiver_object));
    param.set_base_name(base);
    symbolt param_symbol = make_symbol(
      param.get_identifier(),
      base,
      receiver_type,
      location_of(decl),
      parse_tree.package);
    param_symbol.is_parameter = true;
    param_symbol.is_lvalue = true;
    param_symbol.is_thread_local = true;
    param_symbol.is_file_local = true;
    add_symbol(param_symbol);
    code_type.parameters().insert(code_type.parameters().begin(), param);
  }

  // The object's name, which for instances of generic functions includes
  // the type arguments (e.g., "Sum[int]").
  symbolt symbol = make_symbol(
    symbol_id(object_id),
    object.get("name"),
    type,
    location_of(decl),
    parse_tree.package);
  add_symbol(symbol);
}

exprt go_convertert::declare_local(
  const irep_idt &object_id,
  const typet &type,
  const source_locationt &location,
  code_blockt &dest)
{
  const irept &object = parse_tree.object(object_id);
  const bool on_heap = state.heap_objects.count(object_id) != 0;
  // A heap variable is accessed through a pointer, named after it.
  const irep_idt id = id2string(symbol_id(object_id)) + (on_heap ? "$ptr" : "");
  const irep_idt base = id2string(object.get("name")) + (on_heap ? "$ptr" : "");
  symbolt symbol = make_symbol(
    id,
    base,
    on_heap ? typet{pointer_typet{type, config.ansi_c.pointer_width}} : type,
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
  if(!on_heap)
    return std::move(result);

  // Go1.27 §Variable_declarations: each execution of the declaration
  // creates a new variable, so allocate it here.
  code_frontend_assignt allocate{result, allocation(type, location)};
  allocate.add_source_location() = location;
  dest.add(std::move(allocate));
  state.heap_pointers[object_id] = result;
  dereference_exprt variable{result};
  variable.add_source_location() = location;
  return std::move(variable);
}

exprt go_convertert::allocation(
  const typet &type,
  const source_locationt &location)
{
  auto size = size_of_expr(type, ns);
  CHECK_RETURN(size.has_value());
  // Zeroed (Go1.27 §The_zero_value), also where the value is set right
  // away: the value set of an arbitrary value has all objects in it, which
  // the value sets of objects shared between goroutines would keep.
  side_effect_exprt result{
    ID_allocate,
    {std::move(*size), true_exprt{}},
    pointer_typet{type, config.ansi_c.pointer_width},
    location};
  return std::move(result);
}

symbol_exprt go_convertert::new_heap_variable(
  const typet &type,
  const exprt &value,
  const source_locationt &location,
  code_blockt &dest)
{
  symbol_exprt pointer = new_temporary(
    pointer_typet{type, config.ansi_c.pointer_width}, location, dest);
  code_frontend_assignt allocate{pointer, allocation(type, location)};
  allocate.add_source_location() = location;
  dest.add(std::move(allocate));
  code_frontend_assignt initialise{
    dereference_exprt{pointer},
    value.is_nil() ? *zero_initializer(type, location, ns)
                   : assign_cast(value, type)};
  initialise.add_source_location() = location;
  dest.add(std::move(initialise));
  return pointer;
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
      if(use_of_unsupported_declaration(object_id, go_location(ident)))
        return nil_exprt{};
      if(!is_converted_package(object.get("pkg")))
        unsupported(
          ident, "function values from package " + object.get_string("pkg"));
      return nil_exprt{};
    }
    exprt result = function_value(*function, location_of(ident));
    // The Go type of the value (for interface values holding it).
    const typet type = convert_type(ident.get("_t"), ident);
    if(type.is_not_nil())
      result.type() = type;
    return result;
  }
  if(kind != "var")
  {
    unsupported(
      ident,
      "use of " + id2string(kind) + " '" + ident.get_string("Name") +
        "' as a value");
    return nil_exprt{};
  }
  const auto heap = state.heap_pointers.find(object_id);
  if(heap != state.heap_pointers.end())
  {
    dereference_exprt result{heap->second};
    result.add_source_location() = location_of(ident);
    return std::move(result);
  }
  const symbolt *symbol = symbol_table.lookup(symbol_id(object_id));
  if(
    symbol == nullptr &&
    use_of_unsupported_declaration(object_id, go_location(ident)))
    return nil_exprt{};
  if(symbol == nullptr)
  {
    // A variable of this package whose declaration failed has already been
    // reported; only report variables of other packages.
    if(!is_converted_package(object.get("pkg")) || !error_found)
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

exprt go_convertert::function_value(
  const symbolt &function,
  const source_locationt &location)
{
  // Function values take an environment before their parameters (see
  // go_type_convertert::func_value); a declared function is called through
  // an adapter that ignores it.
  const code_typet &code = to_code_type(function.type);
  const struct_typet value_type = go_type_convertert::func_value(code);
  const irep_idt id = id2string(function.name) + "$value";
  if(!symbol_table.has_symbol(id))
  {
    code_typet adapter_type = code;
    adapter_type.parameters().insert(
      adapter_type.parameters().begin(),
      code_typet::parametert{typet{go_type_convertert::env_pointer_type()}});
    exprt::operandst arguments;
    std::size_t index = 0;
    for(auto &parameter : adapter_type.parameters())
    {
      const irep_idt base = "$param" + std::to_string(index++);
      const irep_idt param_id = id2string(id) + "::" + id2string(base);
      parameter.set_identifier(param_id);
      parameter.set_base_name(base);
      symbolt param_symbol = make_symbol(
        param_id,
        base,
        parameter.type(),
        function.location,
        id2string(function.module));
      param_symbol.is_parameter = true;
      param_symbol.is_lvalue = true;
      param_symbol.is_thread_local = true;
      param_symbol.is_file_local = true;
      add_symbol(param_symbol);
      if(index > 1)
        arguments.push_back(param_symbol.symbol_expr());
    }
    side_effect_expr_function_callt call{
      function.symbol_expr(),
      std::move(arguments),
      code.return_type(),
      function.location};
    code_blockt body;
    if(code.return_type().id() == ID_empty)
      body.add(expression_statement(std::move(call), function.location));
    else
      body.add(code_frontend_returnt{std::move(call)});
    symbolt adapter = make_symbol(
      id,
      id2string(function.base_name) + "$value",
      adapter_type,
      function.location,
      id2string(function.module));
    adapter.value = std::move(body);
    add_symbol(adapter);
  }
  const auto &components = value_type.components();
  struct_exprt result{
    {assign_cast(
       address_of_exprt{ns.lookup(id).symbol_expr()}, components[0].type()),
     null_pointer_exprt{to_pointer_type(components[1].type())}},
    value_type};
  result.add_source_location() = location;
  return std::move(result);
}

// -------------------------------------------------------------- functions

/// Collect the ids of the objects that \p node declares (`_def`, and
/// `_impl` for implicitly declared ones such as the per-clause variables of
/// a type switch) and the identifiers that refer to objects, including in
/// nested function literals.
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
  if(!node.get("_impl").empty())
    declared.insert(node.get("_impl"));
  for(const auto &sub : node.get_sub())
    collect_objects(sub, declared, uses);
  for(const auto &named : node.get_named_sub())
    collect_objects(named.second, declared, uses);
}

/// The variables a function literal \p lit captures (Go1.27
/// §Function_literals): local variables it refers to that it does not
/// declare itself, in order of first use.
static std::vector<irep_idt>
captured_variables(const irept &lit, const go_parse_treet &parse_tree)
{
  std::set<irep_idt> declared;
  std::vector<const irept *> uses;
  collect_objects(lit, declared, uses);
  // Parameters and results are declared by the literal's signature.
  const irept &signature = parse_tree.type_entry(lit.get("_t"));
  for(const char *list : {"params", "results"})
    for(const auto &item : go_list(signature, list))
      if(!item.get("obj").empty())
        declared.insert(item.get("obj"));
  std::vector<irep_idt> captured;
  std::set<irep_idt> seen;
  for(const irept *use : uses)
  {
    const irep_idt &id = use->get("_obj");
    const irept &object = parse_tree.object(id);
    if(
      object.get("kind") == "var" && !object.get_bool("package_level") &&
      declared.count(id) == 0 && seen.insert(id).second)
    {
      captured.push_back(id);
    }
  }
  return captured;
}

std::vector<irep_idt>
range_body_variables(const irept &stmt, const go_parse_treet &parse_tree)
{
  // The iteration variables (declared with ":=", or assigned), and the
  // body, become the yield function.
  std::set<irep_idt> declared;
  std::vector<const irept *> uses;
  for(const char *field : {"Key", "Value", "Body"})
    collect_objects(go_field(stmt, field), declared, uses);
  std::vector<irep_idt> captured;
  std::set<irep_idt> seen;
  for(const irept *use : uses)
  {
    const irep_idt &id = use->get("_obj");
    const irept &object = parse_tree.object(id);
    if(
      object.get("kind") == "var" && !object.get_bool("package_level") &&
      declared.count(id) == 0 && seen.insert(id).second)
    {
      captured.push_back(id);
    }
  }
  return captured;
}

void go_convertert::scan_heap_variables(const irept &node)
{
  // Go1.27 §Variables: a variable lives as long as it is accessible.
  // Local variables whose address is taken, and those that function
  // literals capture ("Those variables are then shared between the
  // surrounding function and the function literal, and they survive as
  // long as they are accessible", §Function_literals), may outlive the
  // function, so they are allocated (as gc does after escape analysis).
  if(node.id() == "UnaryExpr" && node.get("Op") == "&")
  {
    // The variable at the root of the operand: x in &x, &x.f, &x[i]
    // (for an array x), but not p in &p.f or &p[i] for a pointer p.
    const irept *root = &go_field(node, "X");
    while(true)
    {
      if(root->id() == "ParenExpr")
        root = &go_field(*root, "X");
      else if(
        root->id() == "SelectorExpr" && !root->find("_sel").is_nil() &&
        !root->find("_sel").get_bool("indirect"))
        root = &go_field(*root, "X");
      else if(
        root->id() == "IndexExpr" &&
        type_converter.underlying(go_field(*root, "X").get("_t")).get("k") ==
          "array")
        root = &go_field(*root, "X");
      else
        break;
    }
    if(root->id() == "Ident")
    {
      const irept &object = parse_tree.object(root->get("_obj"));
      if(object.get("kind") == "var" && !object.get_bool("package_level"))
        state.heap_objects.insert(root->get("_obj"));
    }
  }
  else if(node.id() == "FuncLit")
  {
    for(const auto &id : captured_variables(node, parse_tree))
      state.heap_objects.insert(id);
  }
  else if(
    node.id() == "RangeStmt" &&
    type_converter.underlying(go_field(node, "X").get("_t")).get("k") ==
      "signature")
  {
    // The body becomes a function (convert_range_func).
    for(const auto &id : range_body_variables(node, parse_tree))
      state.heap_objects.insert(id);
  }
  else if(
    node.id() == "SelectorExpr" && node.find("_sel").get("kind") == "method")
  {
    // Go1.27 §Calls: "If x is addressable and &x's method set contains m,
    // x.m() is shorthand for (&x).m()".
    const irept &method = parse_tree.object(node.find("_sel").get("obj"));
    const irept &recv = parse_tree.type_entry(method.get("type")).find("recv");
    if(
      recv.is_not_nil() &&
      type_converter.underlying(recv.get("type")).get("k") == "pointer")
    {
      irept address{"UnaryExpr"};
      address.set("Op", "&");
      address.add("X") = go_field(node, "X");
      scan_heap_variables(address);
    }
  }
  else if(
    node.id() == "SliceExpr" &&
    type_converter.underlying(go_field(node, "X").get("_t")).get("k") ==
      "array")
  {
    // Slicing an array refers to it (Go1.27 §Slice_expressions: "If the
    // sliced operand is an array, it must be addressable").
    irept address{"UnaryExpr"};
    address.set("Op", "&");
    address.add("X") = go_field(node, "X");
    scan_heap_variables(address);
  }
  for(const auto &sub : node.get_sub())
    scan_heap_variables(sub);
  for(const auto &named : node.get_named_sub())
    scan_heap_variables(named.second);
}

/// The type of the environment of a function literal that captures the
/// variables \p captured: a struct of pointers to them.
static struct_typet environment_type(
  const std::vector<irep_idt> &captured,
  const std::vector<typet> &types)
{
  struct_typet::componentst components;
  for(std::size_t i = 0; i < captured.size(); ++i)
  {
    components.emplace_back(
      "c" + std::to_string(i),
      pointer_typet{types[i], config.ansi_c.pointer_width});
  }
  return struct_typet{std::move(components)};
}

void go_convertert::convert_function_body(const irept &decl)
{
  const irept &name = go_field(decl, "Name");
  const irep_idt &object_id = name.get("_obj");
  if(!symbol_table.has_symbol(symbol_id(object_id)))
    return; // declaration failed and was reported
  if(is_intrinsic(object_id))
    return; // see finish_intrinsics
  const irep_idt function = symbol_id(object_id);
  const source_locationt location = location_of(decl);
  std::vector<std::string> messages;
  if(go_field(decl, "Body").is_nil())
  {
    messages.push_back(
      "Go front-end: " + id2string(object_id) +
      ", a function without a body, is not supported yet");
    log.warning().source_location = location;
    log.warning() << messages.back() << messaget::eom;
  }
  else
  {
    unsupported_in_body = &messages;
    convert_function(
      function,
      parse_tree.object(object_id).get("type"),
      go_field(decl, "Body"),
      location);
    unsupported_in_body = nullptr;
  }
  if(messages.empty())
    return;

  // Unsupported constructs are never dropped silently: calling the
  // function fails a property that names the first of them. Programs
  // that do not call it (such as most programs using a package that has
  // it) are not affected.
  log.warning().source_location = location;
  log.warning() << "Go front-end: calls of " << function
                << " fail (property class \"unsupported\")" << messaget::eom;
  source_locationt function_location = location;
  function_location.set_function(function);
  symbol_table.get_writeable_ref(function).value =
    unsupported_body(function_location, messages.front());
}

exprt go_convertert::unmodelled_call(
  const irept &call,
  const irep_idt &function_object)
{
  const irept &object = parse_tree.object(function_object);
  return unsupported_call(
    call,
    "a call of " + id2string(function_object) + " (package " +
      object.get_string("pkg") + ", which has no model)");
}

exprt go_convertert::unsupported_call(
  const irept &call,
  const std::string &what)
{
  // If the call is reached, a property of class "unsupported" fails. Its
  // arguments are not evaluated, which can only make that property fail
  // more often.
  const source_locationt location = location_of(call);
  const std::string message = "Go front-end: " + what + " is not supported yet";
  // The result type of the function value, which for generic functions is
  // that of the instance.
  const typet result =
    type_converter.result_type(go_field(call, "Fun").get("_t"));
  if(result.is_nil())
  {
    unsupported(call, type_converter.unsupported_reason);
    return nil_exprt{};
  }
  log.warning().source_location = location;
  log.warning() << message << messaget::eom;
  source_locationt property = location;
  property.set_function(state.function_id);
  code_blockt prefix = unsupported_body(property, message);
  if(result.id() == ID_empty)
  {
    prefix.add(code_skipt{});
    return side_effect_expr_statement_expressiont{
      std::move(prefix), empty_typet{}, location};
  }
  return with_prefix(
    side_effect_expr_nondett{result, location}, prefix, location);
}

bool go_convertert::is_unmodelled_method(const irep_idt &method_object) const
{
  if(symbol_table.has_symbol(symbol_id(method_object)))
    return false;
  const irept &object = parse_tree.object(method_object);
  if(object.get("kind") != "func" || is_converted_package(object.get("pkg")))
    return false;
  const irept &receiver =
    parse_tree.type_entry(object.get("type")).find("recv");
  if(receiver.is_nil())
    return false;
  irept type = type_converter.underlying(receiver.get("type"));
  if(type.get("k") == "pointer")
    type = type_converter.underlying(type.get("elem"));
  // Methods of interfaces are called through dispatchers.
  return type.get("k") != "interface";
}

code_blockt go_convertert::unsupported_body(
  const source_locationt &location,
  const std::string &message)
{
  source_locationt property = location;
  property.set_property_class("unsupported");
  property.set_comment(message);
  code_blockt body;
  code_assertt fail{false_exprt{}};
  fail.add_source_location() = property;
  body.add(std::move(fail));
  code_assumet stop{false_exprt{}};
  stop.add_source_location() = location;
  body.add(std::move(stop));
  body.add_source_location() = location;
  return body;
}

void go_convertert::convert_function(
  const irep_idt &function,
  const irep_idt &signature_index,
  const irept &body_node,
  const source_locationt &location,
  const std::vector<irep_idt> &captured,
  const std::vector<typet> *captured_types,
  std::shared_ptr<range_bodyt> range_body,
  const std::function<void(code_blockt &)> &prologue)
{
  state = function_statet{};
  state.function_id = function;
  state.range_body = std::move(range_body);
  const code_typet &code_type = to_code_type(ns.lookup(function).type);
  state.return_type = code_type.return_type();
  scan_heap_variables(body_node);

  code_blockt body;
  const irept &signature = parse_tree.type_entry(signature_index);

  // Function literals reach the variables they capture through their
  // environment, the first parameter (see convert_func_lit).
  if(!captured.empty())
  {
    std::vector<typet> types;
    if(captured_types != nullptr)
      types = *captured_types;
    else
    {
      for(const auto &id : captured)
        types.push_back(
          convert_type(parse_tree.object(id).get("type"), body_node));
    }
    for(const auto &type : types)
      if(type.is_nil())
        return;
    const struct_typet env_type = environment_type(captured, types);
    const exprt env_param{symbol_exprt{
      code_type.parameters().front().get_identifier(),
      code_type.parameters().front().type()}};
    const dereference_exprt env{typecast_exprt{
      env_param, pointer_typet{env_type, config.ansi_c.pointer_width}}};
    for(std::size_t i = 0; i < captured.size(); ++i)
      state.heap_pointers[captured[i]] =
        member_exprt{env, env_type.components()[i]};
  }

  // Parameters whose address is taken, or that a literal captures, are
  // copied to the heap on entry.
  std::vector<irept> heap_params{
    go_list(signature, "params").begin(), go_list(signature, "params").end()};
  if(signature.find("recv").is_not_nil())
    heap_params.push_back(signature.find("recv"));
  for(const auto &param : heap_params)
  {
    const irep_idt &object_id = param.get("obj");
    if(object_id.empty() || state.heap_objects.count(object_id) == 0)
      continue;
    const symbolt &symbol = ns.lookup(symbol_id(object_id));
    state.heap_pointers[object_id] =
      new_heap_variable(symbol.type, symbol.symbol_expr(), location, body);
  }

  // Go1.27 §Return_statements: "Regardless of how they are declared, all
  // the result values are initialized to the zero values for their type
  // upon entry to the function." Named results are local variables.
  for(const auto &result : go_list(signature, "results"))
  {
    const irep_idt &result_object = result.get("obj");
    if(result_object.empty())
      continue;
    typet type = convert_type(result.get("type"), body_node);
    if(type.is_nil())
      return;
    const exprt var = declare_local(result_object, type, location, body);
    state.named_results.push_back(var);
    body.add(code_frontend_assignt{var, *zero_initializer(type, location, ns)});
  }
  begin_function_panics(body_node, body);

  // The statements are a block of their own, so that jumping to the end of
  // the function (label $unwind) leaves the scopes of their variables
  // rather than entering them.
  code_blockt statements;
  if(prologue)
    prologue(statements);
  code_blockt converted = convert_block(body_node);
  for(auto &statement : converted.statements())
    statements.add(std::move(statement));
  if(state.range_body)
  {
    // The end of the body of a range over a function: the next iteration.
    range_body_exit(RANGE_EXIT_NONE, location, statements);
  }
  add_panic_checks(statements);
  body.add(std::move(statements));
  body.add_source_location() = location;
  end_function_panics(body);
  symbol_table.get_writeable_ref(function).value = std::move(body);
}

void go_convertert::convert_package_init(
  const go_parse_treet::packaget &package,
  const std::vector<irep_idt> &init_functions)
{
  const irep_idt init_id = package_init_id(package.path);
  state = function_statet{};
  state.function_id = init_id;
  code_blockt body;

  // Go1.27 §Package_initialization: package-level variables are
  // initialised stepwise, each step selecting the earliest variable in
  // declaration order that has no dependencies on uninitialised variables;
  // go/types computes this order (types.Info.InitOrder).
  for(const auto &init : package.init_order)
  {
    // As in function bodies (convert_function_body), an unsupported
    // initialiser fails a property of class "unsupported" when the
    // initialisation reaches it.
    std::vector<std::string> messages;
    unsupported_in_body = &messages;
    exprt rhs = convert_expr(init.rhs);
    unsupported_in_body = nullptr;
    if(!messages.empty())
    {
      source_locationt location = location_of(init.rhs);
      location.set_function(init_id);
      body.append(unsupported_body(location, messages.front()));
      break;
    }
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
        lhs->symbol_expr(), assign_cast(values[i], lhs->type)};
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
  // A panic ends the initialisation (and the program).
  add_panic_checks(body);
  end_generated_function(body, empty_typet{});

  symbolt symbol = make_symbol(
    init_id,
    "$init",
    code_typet{{}, empty_typet{}},
    source_locationt{},
    package.path);
  symbol.value = std::move(body);
  add_symbol(symbol);
}

void go_convertert::convert_program_init()
{
  // Go1.27 §Package_initialization: "Package initialization—variable
  // initialization and the invocation of init functions—happens in a single
  // goroutine, sequentially, one package at a time."
  state = function_statet{};
  state.function_id = program_init_id();
  code_blockt body;
  for(const auto &package : parse_tree.packages)
  {
    const symbolt &init = ns.lookup(package_init_id(package.path));
    body.add(code_expressiont{side_effect_expr_function_callt{
      init.symbol_expr(), {}, empty_typet{}, init.location}});
  }
  add_panic_checks(body);
  end_generated_function(body, empty_typet{});
  symbolt symbol = make_symbol(
    program_init_id(),
    "$init",
    code_typet{{}, empty_typet{}},
    source_locationt{},
    parse_tree.package);
  symbol.value = std::move(body);
  add_symbol(symbol);
}

/// Whether \p node refers to the object \p object_id.
static bool mentions_object(const irept &node, const irep_idt &object_id)
{
  if(node.get("_obj") == object_id)
    return true;
  for(const auto &sub : node.get_sub())
    if(mentions_object(sub, object_id))
      return true;
  for(const auto &named : node.get_named_sub())
    if(mentions_object(named.second, object_id))
      return true;
  return false;
}

bool go_convertert::operator()()
{
  // As for C, CBMC's floating-point passes (adjust_float_expressions) read
  // the rounding mode from __CPROVER_rounding_mode. Go always rounds to
  // nearest, ties to even (Go1.27 §Numeric_types: IEEE 754 types).
  if(!symbol_table.has_symbol(CPROVER_PREFIX "rounding_mode"))
  {
    symbolt rounding{CPROVER_PREFIX "rounding_mode", signed_int_type(), "go"};
    rounding.base_name = CPROVER_PREFIX "rounding_mode";
    rounding.is_static_lifetime = true;
    rounding.is_lvalue = true;
    rounding.is_thread_local = true;
    rounding.value = from_integer(0, signed_int_type()); // ROUND_TO_EVEN
    symbol_table.add(rounding);
  }
  // As for C (cprover.h), goto-symex accesses memory at integer addresses
  // through __CPROVER_memory, e.g., when a pointer derived from nil is
  // dereferenced on a path that is not feasible.
  if(!symbol_table.has_symbol(CPROVER_PREFIX "memory"))
  {
    symbolt memory{
      CPROVER_PREFIX "memory",
      array_typet{unsigned_char_type(), infinity_exprt{size_type()}},
      "go"};
    memory.base_name = CPROVER_PREFIX "memory";
    memory.is_static_lifetime = true;
    memory.is_lvalue = true;
    memory.is_extern = true;
    symbol_table.add(memory);
  }

  // Go1.27 §Handling_panics: the state of panicking, and whether recover
  // can stop it (only if the program calls recover).
  declare_panic_state();
  for(const auto &file : parse_tree.asts)
    uses_recover |= mentions_object(file, "recover");

  for(const auto &package : parse_tree.packages)
    converted_packages.insert(package.path);
  // Each package's init functions, in source order.
  std::map<irep_idt, std::vector<irep_idt>> init_functions;
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
        {
          const irep_idt &id = name.get("_obj");
          init_functions[parse_tree.object(id).get("pkg")].push_back(id);
        }
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
  for(const auto &package : parse_tree.packages)
    convert_package_init(package, init_functions[package.path]);
  convert_program_init();
  convert_entry();
  finish_nondet_interfaces();
  finish_reflect();
  finish_errors_as();
  finish_interfaces();
  finish_intrinsics();
  finish_panics();

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
  else if(kind == "RangeStmt")
    convert_range(stmt, dest);
  else if(kind == "TypeSwitchStmt")
    convert_type_switch(stmt, dest);
  else if(kind == "LabeledStmt")
    convert_labeled(stmt, dest);
  else if(kind == "BranchStmt")
    convert_branch(stmt, dest);
  else if(kind == "DeferStmt")
    convert_defer(stmt, dest);
  else if(kind == "GoStmt")
    convert_go(stmt, dest);
  else if(kind == "SendStmt")
    convert_send(stmt, dest);
  else if(kind == "SelectStmt")
    convert_select(stmt, dest);
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
  else if(inner.id() == "RangeStmt")
    convert_range(inner, labelled, label.get("_obj"));
  else if(inner.id() == "TypeSwitchStmt")
    convert_type_switch(inner, labelled, label.get("_obj"));
  else if(inner.id() == "SelectStmt")
    convert_select(inner, labelled, label.get("_obj"));
  else
    convert_statement(inner, labelled);
  // The label marks the start of the statement's code, which stays in the
  // enclosing block: a labelled statement does not open a scope of its
  // own, so that the variables it declares (`L: x := ...`) remain in scope
  // for the statements after it.
  code_labelt result{label.get("Name"), code_skipt{}};
  result.add_source_location() = location_of(stmt);
  dest.add(std::move(result));
  for(auto &statement : labelled.statements())
    dest.add(std::move(statement));
}

void go_convertert::convert_branch(const irept &stmt, code_blockt &dest)
{
  const irep_idt &tok = stmt.get("Tok");
  const irept &label = go_field(stmt, "Label");
  const source_locationt location = location_of(stmt);

  if(tok == "goto")
  {
    // Out of the body of a range over a function: after f returns (see
    // range_body_branch).
    if(
      state.range_body &&
      state.range_body->labels.count(label.get("_obj")) == 0)
    {
      return range_body_branch(stmt, dest);
    }
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
  if(target == nullptr && state.range_body)
    return range_body_branch(stmt, dest);
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
  if(tok == "type")
  {
    // Resolved by go/types, except where the helper could not represent
    // the type (local types of generic functions).
    for(const auto &spec : go_list(decl, "Specs"))
    {
      const irept &object =
        parse_tree.object(go_field(spec, "Name").get("_obj"));
      const irept &entry = parse_tree.type_entry(object.get("type"));
      if(entry.get("k") == "unknown" && !entry.get("reason").empty())
        unsupported(spec, entry.get_string("reason"));
    }
    return;
  }
  if(tok == "const")
    return; // folded by go/types
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
      exprt sym =
        declare_local(name.get("_obj"), type, location_of(name), dest);
      // Go1.27 §The_zero_value.
      exprt value = values.empty()
                      ? *zero_initializer(type, location_of(name), ns)
                      : assign_cast(initialisers[i], type);
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

  if(
    tok != "=" && tok != ":=" && lhs.front().id() == "IndexExpr" &&
    type_converter.underlying(go_field(lhs.front(), "X").get("_t")).get("k") ==
      "map")
  {
    // Go1.27 §Assignment_statements: "x op= y" "is equivalent to x = x op (y)
    // but evaluates x only once"; for a map element, the map and the key.
    const std::string op = id2string(tok);
    return convert_map_update(
      lhs.front(), op.substr(0, op.size() - 1), rhs.front(), stmt, dest);
  }
  if(tok != "=" && tok != ":=")
  {
    // Go1.27 §Assignment_statements: "An assignment operation x op= y
    // where op is a binary arithmetic operator is equivalent to x = x op
    // (y) but evaluates x only once."
    access_partst parts;
    exprt target = convert_access(lhs.front(), parts);
    exprt y = convert_expr(rhs.front());
    if(target.is_nil() || y.is_nil())
      return;
    for(auto &code : parts.operands.statements())
      dest.add(std::move(code));
    for(auto &code : parts.checks.statements())
      dest.add(std::move(code));
    const std::string op = id2string(tok);
    exprt value = convert_arithmetic(
      op.substr(0, op.size() - 1),
      target,
      y,
      rhs.front(),
      target.type(),
      location);
    if(value.is_nil())
      return;
    code_frontend_assignt assign{target, value};
    assign.add_source_location() = location;
    dest.add(std::move(assign));
    return;
  }

  // Go1.27 §Assignment_statements: "The assignment proceeds in two phases.
  // First, the operands of index expressions and pointer indirections
  // (including implicit pointer indirections in selectors) on the left and
  // the expressions on the right are all evaluated in the usual order.
  // Second, the assignments are carried out in left-to-right order." So
  // the operands on the left are evaluated first (into temporaries if
  // there are several assignments, which could change them), then the
  // right; the checks for run-time panics on the left (nil pointers,
  // indices out of range) belong to each assignment, so that in the spec's
  // "x[1], x[3] = 4, 5  // set x[1] = 4, then panic setting x[3] = 5."
  std::vector<exprt> targets;
  std::vector<code_blockt> checks;
  bool any_checks = false;
  for(const auto &target : lhs)
  {
    access_partst parts;
    parts.force_temporaries = lhs.size() > 1;
    if(target.id() == "Ident" && target.get("Name") == "_")
      targets.push_back(exprt{ID_skip}); // the blank identifier
    else if(
      target.id() == "IndexExpr" &&
      type_converter.underlying(go_field(target, "X").get("_t")).get("k") ==
        "map")
    {
      // m[k] = v: the map and the key are operands evaluated in phase 1;
      // the entry is set in phase 2.
      exprt map = access_operand(go_field(target, "X"), parts);
      exprt key = access_operand(go_field(target, "Index"), parts);
      if(map.is_nil() || key.is_nil())
        return;
      map = evaluate_once(map, location, parts.operands);
      key = evaluate_once(
        assign_cast(
          key,
          to_pointer_type(
            to_struct_type(to_pointer_type(map.type()).base_type())
              .get_component("keys")
              .type())
            .base_type()),
        location,
        parts.operands);
      targets.push_back(exprt{"go_map_entry", typet{}, {map, key}});
    }
    else if(tok == ":=" && target.get_bool("_def"))
      targets.push_back(exprt{ID_declaration}); // declared below
    else
      targets.push_back(convert_access(target, parts));
    for(auto &code : parts.operands.statements())
      dest.add(std::move(code));
    any_checks |= !parts.checks.statements().empty();
    checks.push_back(std::move(parts.checks));
  }

  std::vector<exprt> values;
  for(const auto &value : rhs)
    values.push_back(convert_expr(value));
  for(const auto &value : values)
    if(value.is_nil())
      return;
  if(lhs.size() != rhs.size())
  {
    // "In the first, the right hand operand is a single multi-valued
    // expression such as a function call".
    values = unpack(values.front(), location, dest);
  }
  else if(lhs.size() > 1 || any_checks)
  {
    // Evaluate the right before any assignment (so that "a, b = b, a"
    // swaps) and before the checks on the left.
    for(auto &value : values)
    {
      if(value.is_constant() || value.id() == ID_go_nil)
        continue;
      if(lhs.size() == 1 && !has_subexpr(value, ID_side_effect))
        continue;
      symbol_exprt tmp = new_temporary(value.type(), location, dest);
      code_frontend_assignt assign{tmp, value};
      assign.add_source_location() = location;
      dest.add(std::move(assign));
      value = tmp;
    }
  }
  for(std::size_t i = 0; i < lhs.size(); ++i)
  {
    const irept &target = lhs[i];
    exprt lvalue = targets[i];
    for(auto &code : checks[i].statements())
      dest.add(std::move(code));
    if(lvalue.id() == ID_skip)
    {
      // Go1.27 §Assignment_statements: the blank identifier ignores the
      // value; the expression is still evaluated.
      dest.add(expression_statement(values[i], location));
      continue;
    }
    if(lvalue.id() == "go_map_entry")
    {
      const exprt &map = lvalue.operands()[0];
      const typet element =
        to_pointer_type(to_struct_type(to_pointer_type(map.type()).base_type())
                          .get_component("vals")
                          .type())
          .base_type();
      const exprt value =
        evaluate_once(assign_cast(values[i], element), location, dest);
      map_insert(map, lvalue.operands()[1], value, target, dest);
      continue;
    }
    if(lvalue.id() == ID_declaration)
    {
      // Go1.27 §Short_variable_declarations: new variables are declared;
      // others (redeclared) are just assigned.
      const irept &object = parse_tree.object(target.get("_obj"));
      typet type = convert_type(object.get("type"), target);
      if(type.is_nil())
        continue;
      lvalue = declare_local(target.get("_obj"), type, location, dest);
    }
    if(lvalue.is_nil())
      continue;
    code_frontend_assignt assign{lvalue, assign_cast(values[i], lvalue.type())};
    assign.add_source_location() = location;
    dest.add(std::move(assign));
  }
}

void go_convertert::convert_map_update(
  const irept &target,
  const irep_idt &op,
  const irept &y_node,
  const irept &stmt,
  code_blockt &dest)
{
  // m[k] op= y and m[k]++: look up the entry (the zero value if absent),
  // combine, and set the entry, evaluating m and k once.
  const source_locationt location = location_of(stmt);
  exprt map = convert_expr(go_field(target, "X"));
  exprt key = convert_expr(go_field(target, "Index"));
  if(map.is_nil() || key.is_nil())
    return;
  map = evaluate_once(map, location, dest);
  const struct_typet &header =
    to_struct_type(to_pointer_type(map.type()).base_type());
  const typet key_type =
    to_pointer_type(header.get_component("keys").type()).base_type();
  const typet element =
    to_pointer_type(header.get_component("vals").type()).base_type();
  key = evaluate_once(assign_cast(key, key_type), location, dest);
  const symbol_exprt found = map_find(map, key, target, dest);
  const member_exprt vals{dereference_exprt{map}, header.get_component("vals")};
  const symbol_exprt old = new_temporary(element, location, dest);
  dest.add(code_frontend_assignt{
    old,
    if_exprt{
      binary_relation_exprt{found, ID_ge, from_integer(0, found.type())},
      dereference_exprt{plus_exprt{vals, found}},
      *zero_initializer(element, location, ns)}});
  exprt y = y_node.is_nil() ? from_integer(1, element) : convert_expr(y_node);
  if(y.is_nil())
    return;
  exprt value = convert_arithmetic(
    op, old, y, y_node.is_nil() ? stmt : y_node, element, location);
  if(value.is_nil())
    return;
  map_insert(map, key, evaluate_once(value, location, dest), stmt, dest);
}

void go_convertert::convert_inc_dec(const irept &stmt, code_blockt &dest)
{
  const irept &x_node = go_field(stmt, "X");
  if(
    x_node.id() == "IndexExpr" &&
    type_converter.underlying(go_field(x_node, "X").get("_t")).get("k") ==
      "map")
  {
    return convert_map_update(
      x_node, stmt.get("Tok") == "++" ? "+" : "-", irept{ID_nil}, stmt, dest);
  }
  // Go1.27 §IncDec_statements: "x++" is "x += 1" (and so wraps on
  // overflow, Go1.27 §Integer_overflow), evaluating x once.
  access_partst parts;
  exprt target = convert_access(go_field(stmt, "X"), parts);
  if(target.is_nil())
    return;
  for(auto &code : parts.operands.statements())
    dest.add(std::move(code));
  for(auto &code : parts.checks.statements())
    dest.add(std::move(code));
  const exprt one = from_integer(1, target.type());
  exprt value = convert_arithmetic(
    stmt.get("Tok") == "++" ? "+" : "-",
    target,
    one,
    stmt,
    target.type(),
    location_of(stmt));
  if(value.is_nil())
    return;
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
  if(cond.is_not_nil())
    cond = as_condition(cond);
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
  if(cond.is_not_nil())
    cond = as_condition(cond);
  // Go1.27 §For_clause: "Each iteration has its own separate declared
  // variable (or variables) [Go 1.22]. The variable used by the first
  // iteration is declared by the init statement. The variable used by each
  // subsequent iteration is declared implicitly before executing the post
  // statement and initialized to the value of the previous iteration's
  // variable at that moment." This is observable only for heap variables
  // (whose address is taken or that a literal captures): allocate a copy.
  code_blockt post_block;
  if(init.id() == "AssignStmt" && init.get("Tok") == ":=")
  {
    for(const auto &name : go_list(init, "Lhs"))
    {
      const auto heap = state.heap_pointers.find(name.get("_obj"));
      if(!name.get_bool("_def") || heap == state.heap_pointers.end())
        continue;
      const exprt &pointer = heap->second;
      const typet &type = to_pointer_type(pointer.type()).base_type();
      symbol_exprt next = new_heap_variable(
        type, dereference_exprt{pointer}, location_of(name), post_block);
      post_block.add(code_frontend_assignt{pointer, next});
    }
  }
  exprt iter = nil_exprt{};
  const irept &post = go_field(stmt, "Post");
  if(post.is_not_nil())
    convert_statement(post, post_block);
  if(!post_block.statements().empty())
  {
    iter = side_effect_expr_statement_expressiont{
      std::move(post_block), empty_typet{}, location_of(stmt)};
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

void go_convertert::convert_range(
  const irept &stmt,
  code_blockt &dest,
  const irep_idt &label_object)
{
  // Go1.27 §For_range, for integers, arrays and pointers to arrays. "The
  // range expression x is evaluated before beginning the loop, with one
  // exception: if at most one iteration variable is present and x or
  // len(x) is constant, the range expression is not evaluated." For an
  // integer n, "the iteration values 0 through n-1 are produced in
  // increasing order"; for an array or pointer to array a, "the index
  // iteration values are produced in increasing order, starting at element
  // index 0. If at most one iteration variable is present, the range loop
  // produces iteration values from 0 up to len(a)-1 and does not index
  // into the array or slice itself."
  const source_locationt location = location_of(stmt);
  const irept &x_node = go_field(stmt, "X");
  const irept &key_node = go_field(stmt, "Key");
  const irept &value_node = go_field(stmt, "Value");
  const bool has_value =
    value_node.is_not_nil() && value_node.get("Name") != "_";
  const irept &under = type_converter.underlying(x_node.get("_t"));
  const irep_idt &k = under.get("k");
  const bool is_string =
    k == "basic" &&
    (under.get("kind") == "string" || under.get("kind") == "untyped string");
  const bool is_slice = k == "slice";
  const bool is_integer = k == "basic" && !is_string;
  const irept *array_type = &under;
  if(k == "pointer")
    array_type = &type_converter.underlying(under.get("elem"));
  if(k == "map")
  {
    exprt map = convert_expr(x_node);
    if(map.is_nil())
      return;
    return convert_map_range(stmt, map, dest, label_object);
  }
  if(k == "chan")
  {
    exprt chan = convert_expr(x_node);
    if(chan.is_nil())
      return;
    return convert_chan_range(stmt, chan, dest, label_object);
  }
  if(k == "signature")
  {
    exprt function = convert_expr(x_node);
    if(function.is_nil())
      return;
    return convert_range_func(stmt, function, dest, label_object);
  }
  if(!is_integer && !is_string && !is_slice && array_type->get("k") != "array")
  {
    unsupported(stmt, "range over values of type " + under.get_string("str"));
    return;
  }

  code_blockt scope;
  // The type of the index: that of n for integers (or of the iteration
  // variable, for an untyped constant n), int for arrays.
  typet index_type;
  exprt count;
  exprt range_value = nil_exprt{};
  if(is_string || is_slice)
  {
    // The string or slice is evaluated once; for slices "For a nil slice,
    // the number of iterations is 0" (its length is 0).
    index_type = signedbv_typet{config.go.int_width};
    exprt value = convert_expr(x_node);
    if(value.is_nil())
      return;
    value = assign_cast(value, convert_type(x_node.get("_t"), x_node));
    symbol_exprt tmp = new_temporary(value.type(), location, scope);
    scope.add(code_frontend_assignt{tmp, value});
    range_value = tmp;
    count = member_exprt{tmp, to_struct_type(tmp.type()).get_component("len")};
  }
  else if(is_integer)
  {
    const irept &type_source =
      type_converter.is_untyped(x_node.get("_t")) && key_node.is_not_nil()
        ? key_node
        : x_node;
    index_type = expr_type(type_source);
    if(index_type.is_nil())
      return;
    exprt n = convert_expr(x_node);
    if(n.is_nil())
      return;
    n = assign_cast(n, index_type);
    if(!n.is_constant())
    {
      symbol_exprt tmp = new_temporary(index_type, location, scope);
      scope.add(code_frontend_assignt{tmp, n});
      n = tmp;
    }
    count = n;
  }
  else
  {
    index_type = signedbv_typet{config.go.int_width};
    count =
      from_integer(string2integer(array_type->get_string("len")), index_type);
    // An array is copied (it is evaluated as a value); a pointer to an
    // array is not. Without a value variable, x is evaluated only if
    // len(x) is not constant, i.e., if it contains calls.
    if(has_value || contains_call(x_node))
    {
      exprt value = convert_expr(x_node);
      if(value.is_nil())
        return;
      symbol_exprt tmp = new_temporary(value.type(), location, scope);
      scope.add(code_frontend_assignt{tmp, value});
      range_value = tmp;
    }
  }

  // The loop, with a hidden index.
  const irep_idt index_id = id2string(state.function_id) + "::$range" +
                            std::to_string(state.label_counter);
  symbolt index_symbol = make_symbol(
    index_id,
    "$range" + std::to_string(state.label_counter),
    index_type,
    location,
    parse_tree.package);
  index_symbol.is_lvalue = true;
  index_symbol.is_thread_local = true;
  index_symbol.is_file_local = true;
  add_symbol(index_symbol);
  const symbol_exprt index = index_symbol.symbol_expr();
  scope.add(code_frontend_declt{index});

  const std::string prefix =
    "$for" + std::to_string(state.label_counter++) + "$";
  code_blockt body;
  // Iteration values: declared anew in each iteration with ":=" ("each
  // iteration has its own new variables [Go 1.22]"), or assigned to
  // preexisting variables "as in an assignment statement".
  const auto bind = [&](const irept &node, const exprt &value)
  {
    if(node.is_nil() || (node.id() == "Ident" && node.get("Name") == "_"))
      return;
    exprt target;
    if(stmt.get("Tok") == ":=")
    {
      typet type =
        convert_type(parse_tree.object(node.get("_obj")).get("type"), node);
      if(type.is_nil())
        return;
      target = declare_local(node.get("_obj"), type, location_of(node), body);
    }
    else
    {
      access_partst parts;
      target = convert_access(node, parts);
      if(target.is_nil())
        return;
      for(auto &code : parts.operands.statements())
        body.add(std::move(code));
      for(auto &code : parts.checks.statements())
        body.add(std::move(code));
    }
    code_frontend_assignt assign{target, assign_cast(value, target.type())};
    assign.add_source_location() = location_of(node);
    body.add(std::move(assign));
  };
  // For strings, the byte index advances by the length of each code point.
  exprt step = from_integer(1, index_type);
  if(is_string)
  {
    // Go1.27 §For_range: "For a string value, the "range" clause iterates
    // over the Unicode code points in the string starting at byte index 0.
    // On successive iterations, the index value will be the index of the
    // first byte of successive UTF-8-encoded code points in the string, and
    // the second value, of type rune, will be the value of the
    // corresponding code point." (rt.DecodeRune)
    const exprt decoded =
      runtime_call("DecodeRune", {range_value, index}, location);
    const symbol_exprt pair = new_temporary(decoded.type(), location, scope);
    const auto &parts = to_struct_type(pair.type()).components();
    body.add(code_frontend_assignt{pair, decoded});
    bind(key_node, index);
    if(has_value)
      bind(value_node, member_exprt{pair, parts[0]});
    step = typecast_exprt::conditional_cast(
      member_exprt{pair, parts[1]}, index_type);
  }
  else
    bind(key_node, index);
  if(is_slice && has_value)
  {
    // The elements are those of the underlying array at each iteration.
    const struct_typet &slice_type = to_struct_type(range_value.type());
    bind(
      value_node,
      dereference_exprt{plus_exprt{
        member_exprt{range_value, slice_type.get_component("data")}, index}});
  }
  else if(has_value && !is_string)
  {
    exprt array = range_value;
    if(k == "pointer")
    {
      // Indexing through a nil pointer panics.
      array.add_source_location() = location;
      check_not_nil(array, body);
      array = dereference_exprt{array};
    }
    bind(value_node, index_exprt{array, typecast_exprt{index, c_index_type()}});
  }

  state.jump_targets.push_back(
    {label_object, true, prefix + "break", prefix + "continue"});
  code_blockt statements = convert_block(go_field(stmt, "Body"));
  state.jump_targets.pop_back();
  for(auto &code : statements.statements())
    body.add(std::move(code));
  if(!label_object.empty())
    body.add(code_labelt{prefix + "continue", code_skipt{}});

  code_blockt advance;
  advance.add(code_frontend_assignt{index, plus_exprt{index, step}});
  code_fort loop{
    code_frontend_assignt{index, from_integer(0, index_type)},
    binary_relation_exprt{index, ID_lt, count},
    side_effect_expr_statement_expressiont{
      std::move(advance), empty_typet{}, location},
    std::move(body)};
  loop.add_source_location() = location;
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
      code_ifthenelset dispatch{
        as_condition(condition), code_gotot{clause_label(i)}};
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
  // In the body of a range over a function, the results are those of the
  // function containing the loop.
  const typet &return_type =
    state.range_body ? state.range_body->return_type : state.return_type;
  code_frontend_returnt ret;
  if(results.empty())
    ret = bare_return();
  else
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
      ret = code_frontend_returnt{assign_cast(values.front(), return_type)};
    else
    {
      const auto &components = to_struct_type(return_type).components();
      exprt::operandst operands;
      for(std::size_t i = 0; i < values.size(); ++i)
        operands.push_back(assign_cast(values[i], components[i].type()));
      ret =
        code_frontend_returnt{struct_exprt{std::move(operands), return_type}};
    }
  }
  emit_return(std::move(ret), location, dest);
}

code_frontend_returnt go_convertert::bare_return() const
{
  // Go1.27 §Return_statements: "The "return" statement returns the values
  // of these variables." In the body of a range over a function, the loop
  // returns them.
  if(state.range_body || state.named_results.empty())
    return code_frontend_returnt{};
  if(state.named_results.size() == 1)
    return code_frontend_returnt{state.named_results.front()};
  exprt::operandst values{
    state.named_results.begin(), state.named_results.end()};
  return code_frontend_returnt{
    struct_exprt{std::move(values), state.return_type}};
}

void go_convertert::emit_return(
  code_frontend_returnt ret,
  const source_locationt &location,
  code_blockt &dest)
{
  ret.add_source_location() = location;
  if(state.range_body)
  {
    // The loop returns the values (see convert_range_func).
    if(ret.has_return_value())
    {
      dest.add(code_frontend_assignt{
        dereference_exprt{state.heap_pointers.at(state.range_body->results_id)},
        ret.return_value()});
      return range_body_exit(RANGE_EXIT_RETURN_VALUES, location, dest);
    }
    return range_body_exit(RANGE_EXIT_RETURN, location, dest);
  }
  if(!state.has_defers)
  {
    dest.add(std::move(ret));
    return;
  }

  // Go1.27 §Defer_statements: "deferred functions are executed after any
  // result parameters are set by that return statement but before the
  // function returns to its caller."
  if(ret.has_return_value())
  {
    if(state.named_results.empty())
    {
      dest.add(
        code_frontend_assignt{state.result_variable, ret.return_value()});
    }
    else if(state.named_results.size() == 1)
    {
      dest.add(
        code_frontend_assignt{state.named_results.front(), ret.return_value()});
    }
    else
    {
      // The values are computed before the results are set: they may refer
      // to the results (return b, a).
      const exprt values = evaluate_once(ret.return_value(), location, dest);
      const auto &components = to_struct_type(state.return_type).components();
      for(std::size_t i = 0; i < state.named_results.size(); ++i)
      {
        dest.add(code_frontend_assignt{
          state.named_results[i], member_exprt{values, components[i]}});
      }
    }
  }
  code_gotot jump{"$unwind"};
  jump.add_source_location() = location;
  dest.add(std::move(jump));
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

// ---------------------------------------------------------- access paths

void go_convertert::check_not_nil(const exprt &pointer, code_blockt &dest)
{
  // Go1.27 §Address_operators: "If x is nil, an attempt to evaluate *x will
  // cause a run-time panic." §Selectors: "If x is of pointer type and has
  // the value nil and x.f denotes a struct field, assigning to or
  // evaluating x.f causes a run-time panic."
  add_panic(
    dest,
    notequal_exprt{
      pointer, null_pointer_exprt{to_pointer_type(pointer.type())}},
    "nil-dereference",
    "runtime error: invalid memory address or nil pointer dereference",
    pointer.source_location());
}

void go_convertert::check_index(
  const exprt &index,
  const mp_integer &size,
  const source_locationt &location,
  code_blockt &dest)
{
  // Go1.27 §Index_expressions: "the index x is in range if 0 <= x < len(a),
  // otherwise it is out of range"; "if x is out of range at run time, a
  // run-time panic occurs". Compare in a type that holds both bounds.
  const signedbv_typet wide{65};
  const typecast_exprt i{index, wide};
  const and_exprt in_range{
    binary_relation_exprt{i, ID_ge, from_integer(0, wide)},
    binary_relation_exprt{i, ID_lt, from_integer(size, wide)}};
  add_panic(
    dest,
    in_range,
    "index-out-of-range",
    "runtime error: index out of range",
    location);
}

exprt go_convertert::access_operand(exprt value, access_partst &parts)
{
  if(value.is_nil() || value.is_constant())
    return value;
  if(!parts.force_temporaries && !has_subexpr(value, ID_side_effect))
    return value;
  const source_locationt location = value.source_location();
  symbol_exprt tmp = new_temporary(value.type(), location, parts.operands);
  code_frontend_assignt assign{tmp, std::move(value)};
  assign.add_source_location() = location;
  parts.operands.add(std::move(assign));
  return std::move(tmp);
}

exprt go_convertert::access_operand(const irept &expr, access_partst &parts)
{
  return access_operand(convert_expr(expr), parts);
}

exprt go_convertert::with_parts(exprt value, access_partst &parts)
{
  code_blockt prefix = std::move(parts.operands);
  for(auto &check : parts.checks.statements())
    prefix.add(std::move(check));
  parts = access_partst{};
  return with_prefix(std::move(value), prefix, value.source_location());
}

exprt go_convertert::convert_access(const irept &expr, access_partst &parts)
{
  const source_locationt location = location_of(expr);
  const irep_idt &kind = expr.id();

  // A constant (e.g., the receiver of time.Second.String()) is a value, not
  // a variable. (The helper does not send the operands of constant
  // expressions, parenthesised ones included.)
  if(!expr.get("_cv").empty() && !expr.get_bool("_tx"))
    return convert_expr(expr);

  if(kind == "ParenExpr")
    return convert_access(go_field(expr, "X"), parts);

  if(kind == "Ident" && expr.get("Name") != "_")
    return variable(expr);

  if(kind == "StarExpr" && !expr.get_bool("_tx"))
  {
    // Go1.27 §Address_operators: "the pointer indirection *x denotes the
    // variable of type T pointed to by x."
    exprt pointer = access_operand(go_field(expr, "X"), parts);
    if(pointer.is_nil())
      return nil_exprt{};
    pointer.add_source_location() = location;
    check_not_nil(pointer, parts.checks);
    dereference_exprt result{pointer};
    result.add_source_location() = location;
    return std::move(result);
  }

  if(kind == "SelectorExpr")
  {
    const irept &selection = expr.find("_sel");
    if(selection.is_nil())
    {
      // A qualified identifier (Go1.27 §Qualified_identifiers), whose
      // type go/types records on the selector expression.
      irept sel = go_field(expr, "Sel");
      if(sel.get("_t").empty())
        sel.set("_t", expr.get("_t"));
      return variable(sel);
    }
    if(selection.get("kind") != "field")
      return access_operand(convert_method_value(expr), parts);
    // Go1.27 §Selectors: x.f denotes the field at the shallowest depth;
    // go/types records the path of field indices through embedded fields,
    // each of which may be a pointer, dereferenced implicitly ("p.x is
    // (*(*p).T0).x").
    const irept &x = go_field(expr, "X");
    exprt current = type_converter.underlying(x.get("_t")).get("k") == "pointer"
                      ? access_operand(x, parts)
                      : convert_access(x, parts);
    if(current.is_nil())
      return nil_exprt{};
    for(const auto &step : go_list(selection, "path"))
    {
      if(current.type().id() == ID_pointer)
      {
        current.add_source_location() = location;
        check_not_nil(current, parts.checks);
        current = dereference_exprt{current};
      }
      const struct_typet &type = to_struct_type(go_follow(ns, current.type()));
      // Path elements are numbers, stored as the ids of their ireps.
      const std::size_t index = std::stoul(step.id_string());
      current = member_exprt{current, type.components()[index]};
      current.add_source_location() = location;
    }
    return current;
  }

  if(kind == "IndexExpr")
  {
    const irept &x = go_field(expr, "X");
    const irept &under = type_converter.underlying(x.get("_t"));
    exprt array;
    if(under.get("k") == "array")
      array = convert_access(x, parts);
    else if(
      under.get("k") == "pointer" &&
      type_converter.underlying(under.get("elem")).get("k") == "array")
    {
      // Go1.27 §Index_expressions: "For a of pointer to array type: a[x] is
      // shorthand for (*a)[x]".
      exprt pointer = access_operand(x, parts);
      if(pointer.is_nil())
        return nil_exprt{};
      pointer.add_source_location() = location;
      check_not_nil(pointer, parts.checks);
      array = dereference_exprt{pointer};
    }
    else if(under.get("k") == "map")
    {
      // Map elements are not variables; reads are lookups.
      return access_operand(
        convert_map_lookup(
          expr, type_converter.underlying(expr.get("_t")).get("k") == "tuple"),
        parts);
    }
    else if(
      under.get("k") == "slice" ||
      (under.get("k") == "basic" && (under.get("kind") == "string" ||
                                     under.get("kind") == "untyped string")))
    {
      // Go1.27 §Index_expressions: for slices, "a[x] is the slice element
      // at index x"; for strings, "a[x] is the non-constant byte value at
      // index x". Both panic if x is out of range.
      exprt value = access_operand(x, parts);
      if(value.is_nil())
        return nil_exprt{};
      if(!value.is_constant() && value.id() != ID_symbol)
      {
        // The operand is used twice (bounds and element).
        symbol_exprt tmp =
          new_temporary(value.type(), location, parts.operands);
        parts.operands.add(code_frontend_assignt{tmp, value});
        value = tmp;
      }
      const irept &index_node = go_field(expr, "Index");
      exprt index = access_operand(index_node, parts);
      if(index.is_nil())
        return nil_exprt{};
      return element(value, index, false, location, parts.checks);
    }
    else
    {
      unsupported(expr, "indexing values of type " + under.get_string("str"));
      return nil_exprt{};
    }
    if(array.is_nil())
      return nil_exprt{};
    const irept &index_node = go_field(expr, "Index");
    exprt index = access_operand(index_node, parts);
    if(index.is_nil())
      return nil_exprt{};
    const auto size =
      numeric_cast<mp_integer>(to_array_type(array.type()).size());
    CHECK_RETURN(size.has_value());
    // "a constant index must be in range" (checked by go/types).
    if(index_node.get("_cv").empty())
      check_index(index, *size, location, parts.checks);
    index_exprt result{
      array, typecast_exprt::conditional_cast(index, c_index_type())};
    result.add_source_location() = location;
    return std::move(result);
  }

  // Any other expression is a value, not a variable.
  return access_operand(expr, parts);
}

/// Whether \p expr denotes an instantiated generic function or method.
static bool is_instantiation(const irept &expr)
{
  const irept *node = &expr;
  while(node->id() == "ParenExpr")
    node = &go_field(*node, "X");
  if(node->id() == "SelectorExpr")
    node = &go_field(*node, "Sel");
  return node->id() == "Ident" && !go_list(*node, "_inst").empty();
}

const irept &skip_instantiation(const irept &expr)
{
  // Go1.27 §Instantiations: "When using a generic function or method, type
  // arguments may be provided explicitly, or they may be partially or
  // completely inferred from the context in which the function is used."
  // Either way the instance is recorded on the function's identifier.
  const irept *node = &expr;
  while(true)
  {
    if(node->id() == "ParenExpr")
      node = &go_field(*node, "X");
    else if(
      (node->id() == "IndexExpr" || node->id() == "IndexListExpr") &&
      !node->get_bool("_tx") && is_instantiation(go_field(*node, "X")))
    {
      node = &go_field(*node, "X");
    }
    else
      return *node;
  }
}

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

bool go_convertert::convert_intrinsic_call(const irept &call, code_blockt &dest)
{
  const irep_idt callee = callee_object(call);
  const auto &args = go_list(call, "Args");
  const source_locationt &location = location_of(call);

  if(callee == "panic")
  {
    // Go1.27 §Handling_panics: "panic" stops the ordinary execution of
    // the function, see raise_panic.
    std::string comment = "panic";
    if(args.size() == 1 && args.front().get("_cvk") == "string")
    {
      const std::string text = args.front().get_string("_cv");
      comment += ": " + text.substr(1, text.size() - 2); // drop quotes
    }
    exprt value = convert_expr(args.front());
    if(value.is_nil())
      return true;
    if(!uses_recover)
    {
      // The value is not observable (see raise_panic), but is evaluated.
      if(value.id() != ID_go_nil)
        dest.add(expression_statement(value, location));
      raise_panic(dest, nil_exprt{}, "panic", comment, location);
      return true;
    }
    value = evaluate_once(
      assign_cast(value, go_type_convertert::interface_type()), location, dest);
    // "calling panic with a nil interface value (or an untyped nil) causes
    // a run-time panic", whose value gc describes as follows.
    const struct_typet iface = go_type_convertert::interface_type();
    code_blockt nil_value;
    nil_value.add(code_frontend_assignt{
      value,
      runtime_error(
        "runtime error: panic called with nil argument", location)});
    dest.add(code_ifthenelset{
      equal_exprt{
        member_exprt{value, iface.components()[0]},
        from_integer(0, iface.components()[0].type())},
      std::move(nil_value)});
    raise_panic(dest, value, "panic", comment, location);
    return true;
  }

  if(callee == "clear")
  {
    convert_clear(call, dest);
    return true;
  }
  if(callee == "close")
  {
    convert_close(call, dest);
    return true;
  }
  if(callee == "delete")
  {
    convert_delete(call, dest);
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
    code_assertt check{as_condition(cond)};
    check.add_source_location() = loc;
    dest.add(std::move(check));
    return true;
  }

  if(callee == "cprover/rt.AtomicBegin" || callee == "cprover/rt.AtomicEnd")
  {
    const bool begin = callee == "cprover/rt.AtomicBegin";
    if(begin)
      ++state.atomic_depth;
    else if(state.atomic_depth > 0)
      --state.atomic_depth;
    codet section{begin ? ID_atomic_begin : ID_atomic_end};
    section.add_source_location() = location;
    dest.add(std::move(section));
    return true;
  }

  if(
    callee == "cprover/rt.RaceRead" || callee == "cprover/rt.RaceWrite" ||
    callee == "cprover/rt.RaceObserve")
  {
    exprt pointer = convert_expr(args.front());
    if(pointer.is_nil())
      return true;
    if(pointer.type().id() != ID_pointer)
    {
      unsupported(
        args.front(),
        "rt." + id2string(callee).substr(11) + " of a non-pointer");
      return true;
    }
    race_marker(
      callee == "cprover/rt.RaceWrite"  ? "go::$race_write"
      : callee == "cprover/rt.RaceRead" ? "go::$race_read"
                                        : "go::$race_observe",
      pointer,
      location,
      dest);
    return true;
  }

  if(callee == "cprover/rt.AtomicBeginWhen")
  {
    // Begin an atomic section once the condition holds (block until then).
    const irept &condition = args.front();
    if(convert_expr(condition).is_nil())
      return true;
    ++state.atomic_depth;
    block_until(
      [this, &condition]() { return as_condition(convert_expr(condition)); },
      true,
      location,
      dest);
    return true;
  }

  if(callee == "cprover/rt.Unsupported")
  {
    // An operation that a model does not support fails a property (as
    // calls of functions without a model do) and stops the path.
    if(args.size() != 1 || args.front().get("_cvk") != "string")
    {
      unsupported(call, "a non-constant description of rt.Unsupported");
      return true;
    }
    const std::string text = args.front().get_string("_cv");
    dest.append(unsupported_body(location, text.substr(1, text.size() - 2)));
    return true;
  }

  if(callee == "cprover/rt.Fatal")
  {
    // A fatal error (as the runtime's fatal, which deferred functions
    // cannot recover from) stops the program.
    if(args.size() != 1 || args.front().get("_cvk") != "string")
    {
      unsupported(call, "a non-constant message of rt.Fatal");
      return true;
    }
    const std::string text = args.front().get_string("_cv");
    source_locationt loc = location;
    loc.set_property_class("fatal");
    loc.set_comment("fatal error: " + text.substr(1, text.size() - 2));
    code_assertt check{false_exprt{}};
    check.add_source_location() = loc;
    dest.add(std::move(check));
    code_assumet stop{false_exprt{}};
    stop.add_source_location() = location;
    dest.add(std::move(stop));
    return true;
  }

  if(callee == "cprover.Assume")
  {
    exprt cond = convert_expr(args[0]);
    if(cond.is_nil())
      return true;
    code_assumet assume{as_condition(cond)};
    assume.add_source_location() = location;
    dest.add(std::move(assume));
    return true;
  }

  return false;
}

/// Convert \p value to \p type, whose underlying types are identical
/// ignoring struct tags (Go1.27 §Conversions); nil if CBMC cannot express
/// it.
static exprt
convert_composite(const exprt &value, const typet &type, const namespacet &ns)
{
  if(value.type() == type)
    return value;
  const typet &from = go_follow(ns, value.type());
  const typet &to = go_follow(ns, type);
  if(from.id() == ID_struct && to.id() == ID_struct)
  {
    // Rebuild the struct field by field.
    const auto &from_components = to_struct_type(from).components();
    const auto &to_components = to_struct_type(to).components();
    if(from_components.size() != to_components.size())
      return nil_exprt{};
    exprt::operandst operands;
    for(std::size_t i = 0; i < to_components.size(); ++i)
    {
      exprt field = convert_composite(
        member_exprt{value, from_components[i]}, to_components[i].type(), ns);
      if(field.is_nil())
        return nil_exprt{};
      operands.push_back(std::move(field));
    }
    return struct_exprt{std::move(operands), type};
  }
  if(from.id() == ID_array && to.id() == ID_array)
  {
    // Arrays whose element types differ only in their Go names have the
    // same CBMC element type.
    if(to_array_type(from).element_type() == to_array_type(to).element_type())
      return typecast_exprt{value, type};
    return nil_exprt{};
  }
  if(
    (from.id() == ID_pointer && to.id() == ID_pointer) ||
    (is_func_value(from) && is_func_value(to)) ||
    (is_go_slice(from) && is_go_slice(to) &&
     from.get_named_sub() == to.get_named_sub()) ||
    ((from.id() == ID_signedbv || from.id() == ID_unsignedbv) &&
     (to.id() == ID_signedbv || to.id() == ID_unsignedbv)) ||
    (from.id() == ID_c_bool && to.id() == ID_c_bool))
  {
    return typecast_exprt::conditional_cast(value, type);
  }
  return nil_exprt{};
}

/// Whether \p type is unsafe.Pointer (see go_type_convertert::basic).
static bool is_unsafe_pointer(const typet &type)
{
  return type.id() == ID_pointer &&
         to_pointer_type(type).base_type().id() == ID_empty;
}

/// If \p value is `uintptr(p) + o1 - o2 ...` for an unsafe.Pointer p,
/// return p moved by the offsets (in bytes); else nil.
static exprt pointer_arithmetic(const exprt &value)
{
  if(value.id() == ID_typecast)
  {
    const exprt &op = to_typecast_expr(value).op();
    if(op.type().id() == ID_pointer)
      return op;
    return nil_exprt{};
  }
  if(value.id() != ID_plus && value.id() != ID_minus)
    return nil_exprt{};
  const auto &binary = to_binary_expr(value);
  const signedbv_typet offset_type{config.ansi_c.pointer_width};
  exprt base = pointer_arithmetic(binary.op0());
  exprt offset = typecast_exprt::conditional_cast(binary.op1(), offset_type);
  if(base.is_nil() && value.id() == ID_plus)
  {
    // o + uintptr(p)
    base = pointer_arithmetic(binary.op1());
    offset = typecast_exprt::conditional_cast(binary.op0(), offset_type);
  }
  if(base.is_nil())
    return nil_exprt{};
  if(value.id() == ID_minus)
    offset = unary_minus_exprt{offset};
  const pointer_typet bytes{unsigned_char_type(), config.ansi_c.pointer_width};
  return typecast_exprt{
    plus_exprt{typecast_exprt::conditional_cast(base, bytes), offset},
    base.type()};
}

exprt go_convertert::convert_unsafe_conversion(
  const exprt &value,
  const typet &type)
{
  // Go1.27 §Package_unsafe: "Any pointer or value of underlying type
  // uintptr can be converted to a type of underlying type Pointer and vice
  // versa." "The effect of converting between Pointer and uintptr is
  // implementation-defined": the integer encodes the pointer as CBMC
  // does (object and offset).
  const bool to_pointer = is_unsafe_pointer(type);
  const bool from_pointer = is_unsafe_pointer(value.type());
  if(to_pointer && value.type().id() == ID_unsignedbv)
  {
    // The arithmetic of the valid patterns of package unsafe (e.g.,
    // unsafe.Pointer(uintptr(p) + offset)) stays within p's object: as
    // pointer arithmetic, so that the result points into it.
    exprt moved = pointer_arithmetic(value);
    if(moved.is_not_nil())
      return typecast_exprt::conditional_cast(moved, type);
    return typecast_exprt{value, type};
  }
  if(from_pointer && type.id() == ID_unsignedbv)
    return typecast_exprt{value, type};
  if(
    (to_pointer && value.type().id() == ID_pointer) ||
    (from_pointer && type.id() == ID_pointer))
  {
    return typecast_exprt::conditional_cast(value, type);
  }
  return nil_exprt{};
}

exprt go_convertert::convert_slice_to_array(
  const exprt &value,
  const typet &type,
  const source_locationt &location)
{
  // Go1.27 §Conversions_from_slice_to_array_or_array_pointer: "Converting a
  // slice to an array yields an array containing the elements of the
  // underlying array of the slice. Similarly, converting a slice to an
  // array pointer yields a pointer to the underlying array of the slice. In
  // both cases, if the length of the slice is less than the length of the
  // array, a run-time panic occurs."
  if(!is_go_slice(value.type()))
    return nil_exprt{};
  const bool to_pointer = type.id() == ID_pointer;
  const typet &array = to_pointer ? to_pointer_type(type).base_type() : type;
  if(array.id() != ID_array)
    return nil_exprt{};
  const array_typet &array_type = to_array_type(array);
  const auto length = numeric_cast<mp_integer>(array_type.size());
  if(!length.has_value())
    return nil_exprt{};

  code_blockt prefix;
  const exprt slice = evaluate_once(value, location, prefix);
  const struct_typet &slice_type = to_struct_type(slice.type());
  const member_exprt len{slice, slice_type.get_component("len")};
  const member_exprt data{slice, slice_type.get_component("data")};
  // gc's message also has the slice's length.
  add_panic(
    prefix,
    binary_relation_exprt{len, ID_ge, from_integer(*length, len.type())},
    "slice-to-array",
    "runtime error: cannot convert slice to array or pointer to array "
    "with length " +
      integer2string(*length),
    location);
  const pointer_typet pointer{array_type, config.ansi_c.pointer_width};
  exprt result;
  if(to_pointer)
  {
    // The data pointer: nil for a nil slice ("t1 == nil" for
    // "(*[0]string)(t)").
    result = typecast_exprt{data, pointer};
  }
  else if(*length == 0)
    result = *zero_initializer(array_type, location, ns);
  else
    result = dereference_exprt{typecast_exprt{data, pointer}};
  result.add_source_location() = location;
  return with_prefix(std::move(result), prefix, location);
}

exprt go_convertert::convert_conversion(const irept &call)
{
  // Go1.27 §Conversions: "When converting between integer types, if the
  // value is a signed integer, it is sign extended to implicit infinite
  // precision; otherwise it is zero extended. It is then truncated to fit
  // in the result type's size." This is CBMC's typecast semantics.
  // Other conversions supported so far change only the type's name: x's
  // type and T have identical underlying types, ignoring struct tags, or
  // are pointer types whose base types do. Constant conversions have
  // already been folded by go/types.
  const auto &args = go_list(call, "Args");
  typet type = expr_type(call);
  exprt value = convert_expr(args.front());
  if(type.is_nil() || value.is_nil())
    return nil_exprt{};
  value = go_cast_nil(value, type, ns);
  if(is_go_interface(type))
  {
    // Go1.27 §Conversions: "x is assignable to T." Converting to an
    // interface type stores the value in an interface value.
    return assign_cast(value, type);
  }
  exprt result = convert_string_conversion(value, type, call);
  if(result.is_not_nil())
    return result;
  if(is_float_like(type) || is_float_like(value.type()))
  {
    result = numeric_conversion(value, type, location_of(call));
    if(result.is_not_nil())
      return result;
  }
  result = convert_unsafe_conversion(value, type);
  if(result.is_not_nil())
  {
    result.add_source_location() = location_of(call);
    return with_go_type(std::move(result), type);
  }
  result = convert_slice_to_array(value, type, location_of(call));
  if(result.is_not_nil())
    return with_go_type(std::move(result), type);
  result = convert_composite(value, type, ns);
  if(result.is_nil())
  {
    unsupported(call, "this conversion");
    return nil_exprt{};
  }
  if(has_subexpr(value, ID_side_effect) && result.id() == ID_struct)
  {
    // The value is used once per field: evaluate it once.
    code_blockt prefix;
    const source_locationt location = location_of(call);
    symbol_exprt tmp = new_temporary(value.type(), location, prefix);
    prefix.add(code_frontend_assignt{tmp, value});
    result = convert_composite(tmp, type, ns);
    result = with_go_type(std::move(result), type);
    return with_prefix(std::move(result), prefix, location);
  }
  result = with_go_type(std::move(result), type);
  result.add_source_location() = location_of(call);
  return result;
}

exprt with_go_type(exprt value, const typet &type)
{
  // A conversion between types with the same representation (e.g., from
  // []int to a named slice type, or from int to a named integer type)
  // leaves the CBMC type unchanged up to the Go type that it records
  // (ID_C_go_tid), on which converting to an interface depends.
  if(
    value.type() == type &&
    value.type().get(ID_C_go_tid) != type.get(ID_C_go_tid))
    value.type() = type;
  return value;
}

exprt go_convertert::convert_min_max(const irept &call, bool is_min)
{
  // Go1.27 §Min_and_max: the smallest (largest) of the arguments, which
  // have the same ordered type. The arguments are evaluated once each, in
  // order (they may have side effects, such as index checks).
  const source_locationt location = location_of(call);
  typet type = expr_type(call);
  if(type.is_nil())
    return nil_exprt{};
  const bool is_float = type.id() == ID_floatbv;
  const bool is_string = is_go_string(type);
  if(
    type.id() != ID_signedbv && type.id() != ID_unsignedbv && !is_float &&
    !is_string)
  {
    unsupported(call, "min/max on non-numeric operands");
    return nil_exprt{};
  }
  code_blockt prefix;
  exprt result = nil_exprt{};
  for(const auto &arg : go_list(call, "Args"))
  {
    exprt value = convert_expr(arg);
    if(value.is_nil())
      return nil_exprt{};
    value = assign_cast(value, type);
    if(has_subexpr(value, ID_side_effect) || is_string)
      value = evaluate_once(value, location, prefix);
    if(result.is_nil())
      result = value;
    else if(is_float)
      result = float_min_max(result, value, is_min);
    else if(is_string)
    {
      // "For string arguments the result for min is the first argument
      // with the smallest (or for max, largest) value, compared lexically
      // byte-wise": keep result unless value is smaller (larger).
      const exprt replace = as_condition(runtime_call(
        "StringLess", is_min ? exprt::operandst{value, result}
                             : exprt::operandst{result, value},
        location));
      result =
        evaluate_once(if_exprt{replace, value, result}, location, prefix);
    }
    else
    {
      const exprt keep =
        is_min ? exprt{binary_relation_exprt{result, ID_le, value}}
               : exprt{binary_relation_exprt{result, ID_ge, value}};
      result = if_exprt{keep, result, value};
    }
  }
  return with_prefix(std::move(result), prefix, location);
}

exprt go_convertert::convert_call(const irept &call)
{
  const irept &fun = go_field(call, "Fun");
  if(fun.get_bool("_tx"))
    return convert_conversion(call);

  const irep_idt callee = callee_object(call);
  const irept &object = parse_tree.object(callee);
  const source_locationt &location = location_of(call);

  {
    // Method calls (also of the predeclared error interface's Error).
    const irept *method = &skip_instantiation(fun);
    if(
      method->id() == "SelectorExpr" &&
      method->find("_sel").get("kind") == "method")
    {
      return convert_method_call(call);
    }
  }
  if(callee == "min" || callee == "max")
    return convert_min_max(call, callee == "min");
  if(callee == "recover")
    return convert_recover(call);
  if(callee == "new")
    return convert_new(call);
  if(callee == "complex" || callee == "real" || callee == "imag")
    return convert_complex_builtin(call, callee);
  if(callee == "len" || callee == "cap")
    return convert_len(call);
  if(callee == "make")
    return convert_make(call);
  if(callee == "append")
    return convert_append(call);
  if(
    callee == "unsafe.Add" || callee == "unsafe.Slice" ||
    callee == "unsafe.SliceData" || callee == "unsafe.String" ||
    callee == "unsafe.StringData")
  {
    return convert_unsafe_call(call, id2string(callee).substr(7));
  }
  if(callee == "copy")
    return convert_copy(call);

  if(callee == "cprover.Nondet")
  {
    // Nondet[T]() yields an arbitrary value of T.
    typet type = expr_type(call);
    if(type.is_nil())
      return nil_exprt{};
    code_blockt prefix;
    exprt value = nondet_value(type, location, prefix);
    if(value.is_nil())
      return nil_exprt{};
    return with_prefix(std::move(value), prefix, location);
  }

  if(object.get("kind") == "builtin")
  {
    unsupported(call, "built-in function " + id2string(callee));
    return nil_exprt{};
  }
  if(object.get("kind") == "func" && !is_converted_package(object.get("pkg")))
    return unmodelled_call(call, callee);

  // Go1.27 §Calls: "In a function call, the function value and arguments
  // are evaluated in the usual order. After they are evaluated, new storage
  // is allocated for the function's variables, which includes its
  // parameters and results. Then, the arguments of the call are passed to
  // the function".
  code_blockt prefix;
  exprt::operandst arguments;

  // A declared function of this package: call it directly.
  const irept *callee_node = &skip_instantiation(fun);
  if(callee_node->id() == "SelectorExpr" && callee_node->find("_sel").is_nil())
    callee_node = &go_field(*callee_node, "Sel"); // a qualified identifier

  if(object.get("kind") == "func" && callee_node->id() == "Ident")
  {
    const symbolt *function = symbol_table.lookup(symbol_id(callee));
    if(function == nullptr)
    {
      const auto it = unsupported_declarations.find(callee);
      if(it != unsupported_declarations.end())
        return unsupported_call(call, it->second);
      return nil_exprt{}; // declaration failed and was reported
    }
    const code_typet &code_type = to_code_type(function->type);
    if(convert_arguments(call, code_type, arguments, prefix))
      return nil_exprt{};
    side_effect_expr_function_callt result{
      function->symbol_expr(),
      std::move(arguments),
      code_type.return_type(),
      location};
    return with_prefix(std::move(result), prefix, location);
  }

  // A function value (see go_type_convertert::func_value).
  exprt value = convert_expr(fun);
  if(value.is_nil())
    return nil_exprt{};
  const typet &value_type = go_follow(ns, value.type());
  PRECONDITION(is_func_value(value_type));
  const auto &components = to_struct_type(value_type).components();
  code_typet code_type =
    to_code_type(to_pointer_type(components[0].type()).base_type());
  code_type.parameters().erase(code_type.parameters().begin()); // $env
  if(has_subexpr(value, ID_side_effect))
  {
    symbol_exprt tmp = new_temporary(value.type(), location, prefix);
    code_frontend_assignt assign{tmp, value};
    assign.add_source_location() = location;
    prefix.add(std::move(assign));
    value = tmp;
  }
  if(convert_arguments(call, code_type, arguments, prefix))
    return nil_exprt{};
  const member_exprt code{value, components[0]};
  const member_exprt env{value, components[1]};

  // "Calling a nil function value causes a run-time panic." The check
  // follows the evaluation of the function value and of the arguments, so
  // evaluate those that have side effects first.
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
    notequal_exprt{code, null_pointer_exprt{to_pointer_type(code.type())}},
    "nil-dereference",
    "runtime error: invalid memory address or nil pointer dereference",
    location);
  arguments.insert(arguments.begin(), env);
  // Instructions generated for the call (e.g., by function-pointer
  // removal) take their location from the function expression.
  dereference_exprt callee_expr{code};
  callee_expr.add_source_location() = location;
  side_effect_expr_function_callt result{
    std::move(callee_expr),
    std::move(arguments),
    code_type.return_type(),
    location};
  return with_prefix(std::move(result), prefix, location);
}

exprt go_convertert::evaluate_once(
  const exprt &value,
  const source_locationt &location,
  code_blockt &dest)
{
  if(value.id() == ID_symbol || value.is_constant())
    return value;
  symbol_exprt tmp = new_temporary(value.type(), location, dest);
  code_frontend_assignt assign{tmp, value};
  assign.add_source_location() = location;
  dest.add(std::move(assign));
  return std::move(tmp);
}

exprt go_convertert::with_prefix(
  exprt value,
  code_blockt &prefix,
  const source_locationt &location)
{
  if(prefix.statements().empty())
    return value;
  // Statement expressions need a value: a call of a function without
  // results is wrapped as is (its type is void).
  const typet type = value.type();
  prefix.add(expression_statement(std::move(value), location));
  return side_effect_expr_statement_expressiont{
    std::move(prefix), type, location};
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
  if(code_type.get_bool(ID_C_go_variadic) && !call.get_bool("_ellipsis"))
  {
    // Go1.27 §Passing_arguments_to_..._parameters: "If f is invoked with no
    // actual arguments for p, the value passed to p is nil. Otherwise, the
    // value passed is a new slice of type []T with a new underlying array
    // whose successive elements are the actual arguments". (With "s...",
    // "it is passed unchanged as the value for a ...T parameter".)
    const std::size_t fixed = parameters.size() - 1;
    if(values.size() < fixed)
    {
      unsupported(call, "this call");
      return true;
    }
    const typet &slice = parameters.back().type();
    const auto &components = to_struct_type(slice).components();
    const typet &element = to_pointer_type(components[0].type()).base_type();
    const std::size_t count = values.size() - fixed;
    const source_locationt location = location_of(call);
    exprt data = null_pointer_exprt{to_pointer_type(components[0].type())};
    if(count > 0)
    {
      // The elements are evaluated (in order) before the array exists.
      for(std::size_t i = fixed; i < values.size(); ++i)
      {
        values[i] =
          evaluate_once(assign_cast(values[i], element), location, prefix);
      }
      data = allocate_elements(
        element, from_integer(count, size_type()), false, location, prefix);
      for(std::size_t i = 0; i < count; ++i)
      {
        prefix.add(code_frontend_assignt{
          dereference_exprt{plus_exprt{data, from_integer(i, c_index_type())}},
          values[fixed + i]});
      }
    }
    const exprt length = from_integer(count, components[1].type());
    values.resize(fixed);
    values.push_back(struct_exprt{{data, length, length}, slice});
  }
  if(values.size() != parameters.size())
  {
    unsupported(call, "this call");
    return true;
  }
  for(std::size_t i = 0; i < values.size(); ++i)
    arguments.push_back(assign_cast(values[i], parameters[i].type()));
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

exprt go_convertert::convert_func_lit(const irept &lit)
{
  // Go1.27 §Function_literals: "A function literal represents an anonymous
  // function." It becomes a function of its own, named after the
  // enclosing one. "Function literals are closures: they may refer to
  // variables declared in a surrounding function. Those variables are then
  // shared between the surrounding function and the function literal, and
  // they survive as long as they are accessible." Captured variables live
  // on the heap (scan_heap_variables); the literal's value carries an
  // environment with pointers to them.
  const std::vector<irep_idt> captured = captured_variables(lit, parse_tree);
  const source_locationt location = location_of(lit);
  std::vector<typet> types;
  exprt::operandst pointers;
  for(const auto &id : captured)
  {
    const auto heap = state.heap_pointers.find(id);
    if(heap == state.heap_pointers.end())
    {
      // Not declared yet (e.g., a forward goto skipped it) or failed.
      unsupported(
        lit,
        "capturing '" + parse_tree.object(id).get_string("name") + "' here");
      return nil_exprt{};
    }
    pointers.push_back(heap->second);
    types.push_back(to_pointer_type(heap->second.type()).base_type());
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
  code_typet &code_type = to_code_type(type);
  declare_parameters(
    code_type, parse_tree.type_entry(signature_index), id, location);
  // The environment comes first (go_type_convertert::func_value).
  const irep_idt env_id = id2string(id) + "::$env";
  code_typet::parametert env_param{
    typet{go_type_convertert::env_pointer_type()}};
  env_param.set_identifier(env_id);
  env_param.set_base_name("$env");
  code_type.parameters().insert(code_type.parameters().begin(), env_param);
  symbolt env_symbol =
    make_symbol(env_id, "$env", env_param.type(), location, parse_tree.package);
  env_symbol.is_parameter = true;
  env_symbol.is_lvalue = true;
  env_symbol.is_thread_local = true;
  env_symbol.is_file_local = true;
  add_symbol(env_symbol);
  symbolt symbol = make_symbol(id, base, type, location, parse_tree.package);
  add_symbol(symbol);

  // The literal's body has its own labels, temporaries and results.
  function_statet outer = std::move(state);
  convert_function(
    id, signature_index, go_field(lit, "Body"), location, captured);
  state = std::move(outer);

  // The value: the code and, if the literal captures variables, a new
  // environment ("each time they are evaluated", like composite literals).
  // The Go type of the value (recorded for interface values holding it).
  const typet value_type = convert_type(signature_index, lit);
  if(value_type.is_nil())
    return nil_exprt{};
  const auto &components = to_struct_type(value_type).components();
  const exprt code = assign_cast(
    address_of_exprt{ns.lookup(id).symbol_expr()}, components[0].type());
  if(captured.empty())
  {
    struct_exprt result{
      {code, null_pointer_exprt{to_pointer_type(components[1].type())}},
      value_type};
    result.add_source_location() = location;
    return std::move(result);
  }
  code_blockt prefix;
  const struct_typet env_type = environment_type(captured, types);
  const symbol_exprt env = new_heap_variable(
    env_type, struct_exprt{std::move(pointers), env_type}, location, prefix);
  struct_exprt result{
    {code, typecast_exprt{env, components[1].type()}}, value_type};
  prefix.add(expression_statement(std::move(result), location));
  return side_effect_expr_statement_expressiont{
    std::move(prefix), value_type, location};
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
  {
    result = value == "true" ? exprt{true_exprt{}} : exprt{false_exprt{}};
    result = typecast_exprt::conditional_cast(result, type);
  }
  else if(
    kind == "int" && (type.id() == ID_signedbv || type.id() == ID_unsignedbv))
    result = from_integer(string2integer(value), type);
  else if(is_float_like(type))
    result = float_constant(expr, type);
  else if(kind == "string" && is_go_string(type))
  {
    // Go1.27 §String_literals; the helper records the exact bytes.
    std::string bytes;
    for(const auto &b : go_list(expr, "_sb"))
      bytes += static_cast<char>(std::stoi(b.id_string()));
    exprt result = string_constant(bytes, location_of(expr));
    result.type() = type; // the Go type, e.g., a named string type
    return result;
  }
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
    x = assign_cast(x, y.type());
  if(y.id() == ID_go_nil && x.id() != ID_go_nil)
    y = assign_cast(y, x.type());
  const source_locationt location = location_of(where);
  if(
    (op == "==" || op == "!=") &&
    (is_go_interface(x.type()) || is_go_interface(y.type())))
  {
    // Go1.27 §Comparison_operators: "A value x of non-interface type X and
    // a value t of interface type T can be compared if type X is comparable
    // and X implements T. They are equal if t's dynamic type is identical to
    // X and t's dynamic value is equal to x." Comparing the interface value
    // that holds x is equivalent.
    const typet iface = go_type_convertert::interface_type();
    exprt result =
      interface_equal(assign_cast(x, iface), assign_cast(y, iface), location);
    if(op == "!=")
      result = not_exprt{result};
    return at_location(std::move(result), location);
  }
  // A copy: x is replaced below.
  const typet type = go_follow(ns, x.type());

  if(is_go_string(type))
  {
    // Go1.27 §Comparison_operators: "String types are comparable and
    // ordered. Two string values are compared lexically byte-wise."
    y = assign_cast(y, x.type());
    exprt result;
    if(op == "==" || op == "!=")
      result = string_equal(x, y, location);
    else if(op == "<" || op == ">=")
      result = as_condition(runtime_call("StringLess", {x, y}, location));
    else
      result = as_condition(runtime_call("StringLess", {y, x}, location));
    if(op == "!=" || op == ">=" || op == "<=")
      result = not_exprt{result};
    return at_location(std::move(result), location);
  }
  if(is_float_like(type))
  {
    // Go1.27 §Comparison_operators: "Floating-point types are comparable
    // and ordered. Two floating-point values are compared as defined by the
    // IEEE 754 standard." Complex types are comparable but not ordered.
    y = assign_cast(y, x.type());
    exprt result;
    if(op == "==" || op == "!=")
    {
      code_blockt prefix;
      if(is_go_complex(type))
      {
        x = evaluate_once(x, location, prefix);
        y = evaluate_once(y, location, prefix);
      }
      result = float_equality(x, y);
      if(op == "!=")
        result = not_exprt{result};
      return with_prefix(
        at_location(std::move(result), location), prefix, location);
    }
    const irep_idt rel = op == "<"    ? ID_lt
                         : op == "<=" ? ID_le
                         : op == ">"  ? ID_gt
                                      : ID_ge;
    return at_location(binary_relation_exprt{x, rel, y}, location);
  }
  if(op == "==" || op == "!=")
  {
    if(is_go_slice(type))
    {
      // Slices may only be compared to nil.
      x = member_exprt{x, to_struct_type(type).components()[0]};
      y = member_exprt{y, to_struct_type(type).components()[0]};
    }
    else if(is_func_value(type))
    {
      // Only comparisons with nil: compare the code pointers.
      x = member_exprt{x, to_struct_type(type).components()[0]};
      y = member_exprt{y, to_struct_type(type).components()[0]};
    }
    else if(
      type.id() == ID_pointer || type.id() == ID_struct ||
      type.id() == ID_array)
    {
      // Equality of composite values compares their parts; evaluate the
      // operands once, in order.
      code_blockt prefix;
      if(has_subexpr(x, ID_side_effect) || has_subexpr(y, ID_side_effect))
      {
        symbol_exprt tx = new_temporary(x.type(), location, prefix);
        prefix.add(code_frontend_assignt{tx, x});
        symbol_exprt ty = new_temporary(y.type(), location, prefix);
        prefix.add(code_frontend_assignt{ty, assign_cast(y, x.type())});
        x = tx;
        y = ty;
      }
      exprt result = equality(x, assign_cast(y, x.type()), where);
      if(result.is_nil())
        return nil_exprt{};
      if(op == "!=")
        result = not_exprt{result};
      result.add_source_location() = location;
      return with_prefix(std::move(result), prefix, location);
    }
  }
  if(
    type.id() != ID_bool && type.id() != ID_c_bool &&
    type.id() != ID_signedbv && type.id() != ID_unsignedbv &&
    !is_func_value(type) && !is_go_slice(type))
  {
    unsupported(
      where, "comparison of type " + x.type().get_string(ID_C_go_type));
    return nil_exprt{};
  }
  if(type.id() == ID_c_bool || type.id() == ID_bool)
  {
    // Any non-zero byte is true.
    x = as_condition(x);
    y = as_condition(y);
  }
  else
    y = assign_cast(y, x.type());
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

/// True if values of \p type contain blank struct fields, which equality
/// ignores (Go1.27 §Comparison_operators).
static bool has_blank_fields(const typet &type, const namespacet &ns)
{
  const typet &followed = go_follow(ns, type);
  if(followed.id() == ID_c_bool)
    return true; // booleans are compared as conditions
  if(followed.id() == ID_floatbv || is_go_complex(followed))
    return true; // IEEE 754 comparison: -0 == +0, NaN != NaN
  if(is_go_string(followed) || is_go_interface(followed))
    return true; // compared by contents, not representation
  if(followed.id() == ID_array)
    return has_blank_fields(to_array_type(followed).element_type(), ns);
  if(followed.id() != ID_struct)
    return false;
  for(const auto &component : to_struct_type(followed).components())
  {
    if(
      has_prefix(id2string(component.get_name()), "_$") ||
      has_blank_fields(component.type(), ns))
    {
      return true;
    }
  }
  return false;
}

exprt go_convertert::equality(
  const exprt &x,
  const exprt &y,
  const irept &where)
{
  const typet &type = go_follow(ns, x.type());
  if(type.id() == ID_pointer)
  {
    // Go1.27 §Comparison_operators: "Two pointer values are equal if they
    // point to the same variable or if both have value nil. Pointers to
    // distinct zero-size variables may or may not be equal."
    const auto size =
      pointer_offset_size(to_pointer_type(type).base_type(), ns);
    exprt same = equal_exprt{x, y};
    if(size.has_value() && *size == 0)
    {
      const null_pointer_exprt null{to_pointer_type(type)};
      same = or_exprt{
        same,
        and_exprt{
          notequal_exprt{x, null},
          notequal_exprt{y, null},
          side_effect_expr_nondett{bool_typet{}, location_of(where)}}};
    }
    return same;
  }
  if(is_go_string(type))
    return string_equal(x, y, location_of(where));
  if(is_go_interface(type))
    return interface_equal(x, y, location_of(where));
  if(type.id() == ID_struct)
  {
    // "Two struct values are equal if their corresponding non-blank field
    // values are equal."
    exprt::operandst conjuncts;
    for(const auto &component : to_struct_type(type).components())
    {
      if(has_prefix(id2string(component.get_name()), "_$"))
        continue;
      exprt part =
        equality(member_exprt{x, component}, member_exprt{y, component}, where);
      if(part.is_nil())
        return nil_exprt{};
      conjuncts.push_back(std::move(part));
    }
    return conjunction(std::move(conjuncts));
  }
  if(type.id() == ID_array)
  {
    // "Two array values are equal if their corresponding element values are
    // equal."
    if(!has_blank_fields(type, ns))
      return equal_exprt{x, y};
    const auto size = numeric_cast<mp_integer>(to_array_type(type).size());
    if(!size.has_value() || *size > 64)
    {
      unsupported(where, "comparing large arrays of structs with blank fields");
      return nil_exprt{};
    }
    exprt::operandst conjuncts;
    for(mp_integer i = 0; i < *size; ++i)
    {
      const exprt index = from_integer(i, c_index_type());
      exprt part =
        equality(index_exprt{x, index}, index_exprt{y, index}, where);
      if(part.is_nil())
        return nil_exprt{};
      conjuncts.push_back(std::move(part));
    }
    return conjunction(std::move(conjuncts));
  }
  if(type.id() == ID_c_bool || type.id() == ID_bool)
    return equal_exprt{as_condition(x), as_condition(y)};
  if(is_float_like(type))
    return float_equality(x, y);
  if(type.id() == ID_signedbv || type.id() == ID_unsignedbv)
    return equal_exprt{x, y};
  unsupported(where, "comparison of type " + x.type().get_string(ID_C_go_type));
  return nil_exprt{};
}

exprt go_convertert::convert_unary(const irept &expr)
{
  // Go1.27 §Arithmetic_operators (unary +, -, ^), §Logical_operators (!).
  const irep_idt &op = expr.get("Op");
  if(op == "<-")
    return convert_receive(expr);
  exprt x = convert_expr(go_field(expr, "X"));
  if(x.is_nil())
    return nil_exprt{};
  exprt result;
  if(op == "+")
    return x;
  else if(op == "-" && is_go_complex(x.type()))
  {
    // Go1.27 §Floating_point_operators: "For floating-point and complex
    // numbers, +x is the same as x, while -x is the negation of x."
    code_blockt block;
    const source_locationt location = location_of(expr);
    x = evaluate_once(x, location, block);
    const auto &parts = to_struct_type(x.type()).components();
    return with_prefix(
      struct_exprt{
        {unary_minus_exprt{member_exprt{x, parts[0]}},
         unary_minus_exprt{member_exprt{x, parts[1]}}},
        x.type()},
      block,
      location);
  }
  else if(op == "-")
    result = unary_minus_exprt{x}; // "0 - x" for unsigned: wraps; floats:
                                   // negation
  else if(op == "^")
    result = bitnot_exprt{x}; // Go1.27: "bitwise complement"
  else if(op == "!")
    result = not_exprt{as_condition(x)};
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
    return and_exprt{as_condition(x), as_condition(y)};
  if(op == "||")
    return or_exprt{as_condition(x), as_condition(y)};

  if(
    op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" ||
    op == ">=")
  {
    return convert_comparison(op, x, y, expr);
  }

  // Arithmetic (Go1.27 §Arithmetic_operators).
  typet type = expr_type(expr);
  if(type.is_nil())
    return nil_exprt{};
  return convert_arithmetic(op, x, y, y_node, type, location);
}

exprt go_convertert::convert_arithmetic(
  const irep_idt &op,
  exprt x,
  exprt y,
  const irept &y_node,
  const typet &type,
  const source_locationt &location)
{
  // Go1.27 §String_concatenation: "Strings can be concatenated using the +
  // operator or the += assignment operator".
  if(is_go_string(type) && op == "+")
    return string_concat(assign_cast(x, type), assign_cast(y, type), location);
  if(is_float_like(type))
    return float_arithmetic(op, x, y, type, location);
  // Only integers so far.
  if(type.id() != ID_signedbv && type.id() != ID_unsignedbv)
  {
    error(
      y_node,
      "operator " + id2string(op) + " on type " +
        type.get_string(ID_C_go_type) + " is not supported yet");
    return nil_exprt{};
  }
  x = assign_cast(x, type);

  const bool is_shift = op == "<<" || op == ">>";
  if(!is_shift)
    y = assign_cast(y, type);

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
    error(y_node, "binary operator " + id2string(op) + " is not supported yet");
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

/// The path of field indices from a struct of type-table entry \p struct_entry
/// to the field with object id \p field, through embedded fields that are
/// not pointers; empty if there is none.
static std::vector<std::size_t> field_path(
  const irep_idt &field,
  const irept &struct_entry,
  const go_type_convertert &types)
{
  const auto &fields = go_list(struct_entry, "fields");
  for(std::size_t i = 0; i < fields.size(); ++i)
    if(fields[i].get("obj") == field)
      return {i};
  for(std::size_t i = 0; i < fields.size(); ++i)
  {
    if(!fields[i].get_bool("embedded"))
      continue;
    const irept &under = types.underlying(fields[i].get("type"));
    if(under.get("k") != "struct")
      continue;
    auto path = field_path(field, under, types);
    if(!path.empty())
    {
      path.insert(path.begin(), i);
      return path;
    }
  }
  return {};
}

/// The path to the field named \p name of the struct \p struct_entry, at
/// the shallowest depth (Go1.27 §Selectors), or empty if there is none
/// (or more than one at that depth).
static std::vector<std::size_t> field_path_by_name(
  const irep_idt &name,
  const irept &struct_entry,
  const go_type_convertert &types)
{
  // Breadth first: the structs at the current depth, with their paths.
  std::vector<std::pair<const irept *, std::vector<std::size_t>>> level{
    {&struct_entry, {}}};
  for(std::size_t depth = 0; depth < 16 && !level.empty(); ++depth)
  {
    std::vector<std::vector<std::size_t>> found;
    std::vector<std::pair<const irept *, std::vector<std::size_t>>> next;
    for(const auto &entry : level)
    {
      const auto &fields = go_list(*entry.first, "fields");
      for(std::size_t i = 0; i < fields.size(); ++i)
      {
        std::vector<std::size_t> path = entry.second;
        path.push_back(i);
        if(fields[i].get("name") == name)
          found.push_back(path);
        if(fields[i].get_bool("embedded"))
        {
          const irept &under = types.underlying(fields[i].get("type"));
          if(under.get("k") == "struct")
            next.emplace_back(&under, std::move(path));
        }
      }
    }
    if(found.size() == 1)
      return found.front();
    if(!found.empty())
      return {};
    level = std::move(next);
  }
  return {};
}

exprt go_convertert::convert_composite_lit(const irept &lit)
{
  // Go1.27 §Composite_literals: "Composite literals construct new values
  // for structs, arrays, slices, and maps each time they are evaluated."
  // Elements are evaluated in order into a temporary that starts as the
  // zero value: "Omitted fields get the zero value for that field", and
  // for arrays "the missing elements are set to the zero value".
  const source_locationt location = location_of(lit);
  const irep_idt &type_index = lit.get("_t");
  const irept &literal_type = type_converter.underlying(type_index);
  if(literal_type.get("k") == "pointer")
  {
    // "elements or keys that are addresses of composite literals may elide
    // the &T when the element or key type is *T."
    irept inner = lit;
    inner.set("_t", literal_type.get("elem"));
    exprt value = convert_composite_lit(inner);
    if(value.is_nil())
      return nil_exprt{};
    code_blockt prefix;
    symbol_exprt pointer =
      new_heap_variable(value.type(), value, location, prefix);
    return with_prefix(std::move(pointer), prefix, location);
  }

  typet type = convert_type(type_index, lit);
  if(type.is_nil())
    return nil_exprt{};
  const irep_idt &k = literal_type.get("k");
  if(lit.find("_cvals").is_not_nil())
    return convert_constant_array_lit(lit, type);
  if(k == "slice")
    return convert_slice_lit(lit, type);
  if(k == "map")
    return convert_map_lit(lit, type);
  if(k != "struct" && k != "array")
  {
    unsupported(
      lit, "composite literals of type " + literal_type.get_string("str"));
    return nil_exprt{};
  }

  code_blockt block;
  symbol_exprt tmp = new_temporary(type, location, block);
  block.add(code_frontend_assignt{tmp, *zero_initializer(type, location, ns)});
  mp_integer next_index = 0;
  std::size_t position = 0;
  for(const auto &element : go_list(lit, "Elts"))
  {
    const bool keyed = element.id() == "KeyValueExpr";
    const irept &value_node = keyed ? go_field(element, "Value") : element;
    exprt target;
    if(k == "struct")
    {
      // "For struct literals without keys, the element list must contain an
      // element for each struct field in the order in which the fields are
      // declared." With keys, "Each key must be a valid field selector
      // [Go 1.27] for a (possibly promoted) field of the struct".
      std::vector<std::size_t> path;
      if(keyed)
      {
        const irept &key = go_field(element, "Key");
        path = field_path(key.get("_obj"), literal_type, type_converter);
        // A source model's literal of its own type, which stands for the
        // real type (whose fields are other objects of the same names).
        if(path.empty())
          path =
            field_path_by_name(key.get("Name"), literal_type, type_converter);
      }
      else
        path = {position++};
      if(path.empty())
      {
        unsupported(element, "this struct literal key");
        return nil_exprt{};
      }
      target = tmp;
      for(const std::size_t index : path)
      {
        const struct_typet &struct_type =
          to_struct_type(go_follow(ns, target.type()));
        target = member_exprt{target, struct_type.components()[index]};
      }
    }
    else
    {
      // "An element with a key uses the key as its index. ... An element
      // without a key uses the previous element's index plus one."
      if(keyed)
        next_index = string2integer(go_field(element, "Key").get_string("_cv"));
      target = index_exprt{tmp, from_integer(next_index, c_index_type())};
      ++next_index;
    }
    exprt value = convert_expr(value_node);
    if(value.is_nil())
      return nil_exprt{};
    code_frontend_assignt assign{target, assign_cast(value, target.type())};
    assign.add_source_location() = location_of(value_node);
    block.add(std::move(assign));
  }
  return with_prefix(std::move(tmp), block, location);
}

exprt go_convertert::convert_address_of(const irept &expr)
{
  // Go1.27 §Address_operators: "For an operand x of type T, the address
  // operation &x generates a pointer of type *T to x." "As an exception to
  // the addressability requirement, x may also be a (possibly
  // parenthesized) composite literal." "Taking the address of a composite
  // literal generates a pointer to a unique variable initialized with the
  // literal's value." "If the evaluation of x would cause a run-time
  // panic, then the evaluation of &x does too."
  const irept *operand = &go_field(expr, "X");
  while(operand->id() == "ParenExpr")
    operand = &go_field(*operand, "X");
  const source_locationt location = location_of(expr);
  if(operand->id() == "CompositeLit")
  {
    exprt value = convert_composite_lit(*operand);
    if(value.is_nil())
      return nil_exprt{};
    code_blockt prefix;
    symbol_exprt pointer =
      new_heap_variable(value.type(), value, location, prefix);
    return with_prefix(std::move(pointer), prefix, location);
  }
  access_partst parts;
  exprt target = convert_access(*operand, parts);
  if(target.is_nil())
    return nil_exprt{};
  address_of_exprt result{
    target, pointer_typet{target.type(), config.ansi_c.pointer_width}};
  result.add_source_location() = location;
  return with_parts(std::move(result), parts);
}

exprt go_convertert::convert_new(const irept &call)
{
  // Go1.27 §Allocation: "The built-in function new creates a new,
  // initialized variable and returns a pointer to it." "If the argument is
  // a type T, then new(T) allocates a variable of type T initialized to its
  // zero value. If the argument is an expression x, then new(x) allocates a
  // variable of the type of x initialized to the value of x."
  const typet type = expr_type(call);
  if(type.is_nil())
    return nil_exprt{};
  const typet &base = to_pointer_type(type).base_type();
  const irept &argument = go_list(call, "Args").front();
  exprt value = nil_exprt{};
  if(!argument.get_bool("_tx"))
  {
    value = convert_expr(argument);
    if(value.is_nil())
      return nil_exprt{};
  }
  const source_locationt location = location_of(call);
  code_blockt prefix;
  symbol_exprt pointer = new_heap_variable(base, value, location, prefix);
  return with_prefix(std::move(pointer), prefix, location);
}

exprt go_convertert::convert_len(const irept &call)
{
  // Go1.27 §Length_and_capacity: len(s) is the "string length in bytes"
  // for strings, the "array length (== n)" for [n]T and *[n]T, the "slice
  // length" for []T; cap(s) is the "slice capacity" for []T. "The length
  // of a nil slice, map or channel is 0. The capacity of a nil slice or
  // channel is 0." For an array or
  // pointer to array, "The expressions len(s) and cap(s) are constants if
  // ... the expression s does not contain channel receives or
  // (non-constant) function calls" (go/types folds those). "Otherwise,
  // invocations of len and cap are not constant and s is evaluated."
  const irept &argument = go_list(call, "Args").front();
  const bool is_cap = callee_object(call) == "cap";
  const irept *under = &type_converter.underlying(argument.get("_t"));
  const typet type = expr_type(call);
  exprt value = convert_expr(argument);
  if(type.is_nil() || value.is_nil())
    return nil_exprt{};
  const source_locationt location = location_of(call);
  if(is_go_chan(value.type()))
    return chan_len(value, is_cap, type, location);
  if(is_go_map(value.type()))
  {
    // For maps, the "map length (number of defined keys)"; nil maps have
    // none.
    code_blockt prefix;
    const exprt map = evaluate_once(value, location, prefix);
    const struct_typet &header =
      to_struct_type(to_pointer_type(map.type()).base_type());
    return with_prefix(
      if_exprt{
        equal_exprt{map, null_pointer_exprt{to_pointer_type(map.type())}},
        from_integer(0, type),
        typecast_exprt::conditional_cast(
          member_exprt{dereference_exprt{map}, header.get_component("len")},
          type)},
      prefix,
      location);
  }
  if(is_go_string(value.type()) || is_go_slice(value.type()))
  {
    const struct_typet &struct_type = to_struct_type(value.type());
    return at_location(
      typecast_exprt::conditional_cast(
        member_exprt{value, struct_type.get_component(is_cap ? "cap" : "len")},
        type),
      location);
  }
  if(under->get("k") == "pointer")
    under = &type_converter.underlying(under->get("elem"));
  if(under->get("k") != "array")
  {
    unsupported(call, "len and cap of type " + under->get_string("str"));
    return nil_exprt{};
  }
  code_blockt prefix;
  prefix.add(expression_statement(std::move(value), location));
  return with_prefix(
    from_integer(string2integer(under->get_string("len")), type),
    prefix,
    location);
}

exprt go_convertert::convert_expr(const irept &expr)
{
  // The value records the Go type of the expression (ID_C_go_tid), on
  // which converting it to an interface depends, also where it is built
  // with a CBMC type that only has the same representation (e.g., len(s),
  // or a map literal).
  exprt result = convert_expr_value(expr);
  const irep_idt &index = expr.get("_t");
  if(result.is_nil() || index.empty() || expr.get_bool("_tx"))
    return result;
  auto cached = go_types.find(index);
  if(cached == go_types.end())
  {
    const std::string reason = type_converter.unsupported_reason;
    cached = go_types.emplace(index, type_converter(index)).first;
    type_converter.unsupported_reason = reason;
  }
  if(cached->second.is_nil())
    return result;
  return with_go_type(std::move(result), cached->second);
}

exprt go_convertert::convert_expr_value(const irept &expr)
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
  if(kind == "$saved")
  {
    // An argument of a deferred call of a built-in function, evaluated
    // at the defer statement (see saved_builtin_call).
    return state.saved_arguments.at(
      numeric_cast_v<std::size_t>(string2integer(expr.get_string("n"))));
  }
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
  if(&skip_instantiation(expr) != &expr)
    return convert_expr(skip_instantiation(expr));
  if(kind == "UnaryExpr" && expr.get("Op") == "&")
    return convert_address_of(expr);
  if(kind == "UnaryExpr")
    return convert_unary(expr);
  if(kind == "CompositeLit")
    return convert_composite_lit(expr);
  if(kind == "SliceExpr")
    return convert_slice_expr(expr);
  if(kind == "TypeAssertExpr")
    return convert_type_assertion(expr);
  if(kind == "StarExpr" || kind == "SelectorExpr" || kind == "IndexExpr")
  {
    access_partst parts;
    exprt result = convert_access(expr, parts);
    if(result.is_nil())
      return nil_exprt{};
    return with_parts(std::move(result), parts);
  }
  if(kind == "BinaryExpr")
    return convert_binary(expr);
  if(kind == "CallExpr")
    return convert_call(expr);

  unsupported(expr, "expression " + id2string(kind));
  return nil_exprt{};
}

void go_convertert::race_marker(
  const irep_idt &id,
  const exprt &pointer,
  const source_locationt &location,
  code_blockt &dest)
{
  if(!race_check)
    return;
  const pointer_typet parameter = pointer_type(empty_typet{});
  if(!symbol_table.has_symbol(id))
  {
    declare_generated(symbol_table, id, {parameter}, empty_typet{});
    symbol_table.get_writeable_ref(id).value = code_blockt{};
  }
  dest.add(code_expressiont{side_effect_expr_function_callt{
    ns.lookup(id).symbol_expr(),
    {typecast_exprt::conditional_cast(pointer, parameter)},
    empty_typet{},
    location}});
}
