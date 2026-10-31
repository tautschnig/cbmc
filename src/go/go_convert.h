/*******************************************************************\

Module: Go front-end: conversion of the Go AST to CBMC symbols

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// Conversion of the in-memory Go AST (go_parse_tree.h) into symbols with
/// `codet` bodies. Semantics follow the Go specification, cited as
/// `Go1.27 §<anchor>` (doc/architectural/go-frontend-plan.md §2.1).
///
/// Constructs that are not supported yet are reported as errors, never
/// silently dropped, so that verification cannot succeed vacuously.

#ifndef CPROVER_GO_GO_CONVERT_H
#define CPROVER_GO_GO_CONVERT_H

#include <util/message.h>
#include <util/mp_arith.h>
#include <util/namespace.h>
#include <util/std_code.h>
#include <util/std_expr.h>

#include "go_types.h"

#include <map>
#include <set>

class go_parse_treet;
class symbol_table_baset;
class symbolt;

/// The Go boolean \p value (stored as a c_bool) as a condition.
exprt as_condition(exprt value);

/// \p value converted to \p type, as for an assignment (Go1.27
/// §Assignability); the untyped nil becomes the nil of \p type.
exprt go_cast(exprt value, const typet &type);

/// True if values of \p type contain pointers or function values, which
/// cannot take arbitrary (nondeterministic) values. Strings and slices
/// can (go_convertert::nondet_value), except, if \p strings_too, at the top
/// level and in struct fields.
bool contains_reference(
  const typet &type,
  const namespacet &ns,
  bool strings_too = true);

/// Converts one Go package.
class go_convertert
{
public:
  go_convertert(
    symbol_table_baset &symbol_table,
    const go_parse_treet &parse_tree,
    message_handlert &message_handler);

  /// \return true on error
  bool operator()();

  /// The CBMC symbol for the Go object with id \p object_id (see
  /// go_symbol_id).
  static irep_idt symbol_id(const irep_idt &object_id)
  {
    return go_symbol_id(object_id);
  }

  /// The generated function that initialises package \p package:
  /// package-level variables in dependency order, then its init functions
  /// (Go1.27 §Package_initialization).
  static irep_idt package_init_id(const std::string &package)
  {
    return "go::" + package + ".$init";
  }

private:
  symbol_table_baset &symbol_table;
  const go_parse_treet &parse_tree;
  messaget log;
  namespacet ns;
  go_type_convertert type_converter;
  bool error_found = false;

  /// A statement that "break" (and, for loops, "continue") can refer to
  /// (Go1.27 §Break_statements, §Continue_statements).
  struct jump_targett
  {
    /// Go object id of the statement's label, or empty.
    irep_idt label_object;
    bool is_loop;
    /// GOTO labels for jumps that native break/continue cannot express:
    /// after the statement, and at the end of a loop body.
    irep_idt break_label;
    irep_idt continue_label;
  };

  /// State of the function being converted; saved while converting the
  /// body of a function literal (Go1.27 §Function_literals).
  struct function_statet
  {
    irep_idt function_id;
    typet return_type;
    /// Result parameters, if named (Go1.27 §Function_types).
    std::vector<exprt> named_results;
    /// Local variables that live on the heap because their address is
    /// taken or a function literal captures them (see scan_heap_variables).
    std::set<irep_idt> heap_objects;
    /// For each heap variable (and each variable a function literal
    /// captures), the pointer to it.
    std::map<irep_idt, exprt> heap_pointers;
    std::size_t tmp_counter = 0;
    std::size_t label_counter = 0;
    std::size_t literal_counter = 0;
    std::vector<jump_targett> jump_targets;

    /// Deferred calls (Go1.27 §Defer_statements): whether the function has
    /// defer statements, and whether they are kept in a list on the heap
    /// (if a defer statement may execute more than once) rather than in
    /// one variable per statement ("slots", with a flag each).
    bool has_defers = false;
    bool defer_list = false;
    std::vector<std::pair<exprt, exprt>> defer_slots; // (flag, function)
    exprt defer_head = nil_exprt{};                   // the list
    std::size_t defer_counter = 0;
    /// With deferred calls, the results are set by return statements and
    /// returned after the deferred calls: the named results, or this.
    exprt result_variable = nil_exprt{};
    /// Whether recover() in this function may stop a panic: the function
    /// was called directly as a deferred function (Go1.27
    /// §Handling_panics).
    exprt may_recover = nil_exprt{};
  };
  function_statet state;

  /// The locations (with property class and comment) of the panics of the
  /// program; panic n is reported as a property at that location if it
  /// ends the program (see finish_panics).
  std::vector<source_locationt> panic_sites;
  /// Whether the program calls recover.
  bool uses_recover = false;

  bool is_converted_package(const irep_idt &package) const;
  /// The source location of \p node, attributed to the current function.
  source_locationt location_of(const irept &node) const;
  void error(const irept &node, const std::string &message);
  /// Add \p symbol; a clash with an existing symbol is an error.
  void add_symbol(const symbolt &symbol);
  void unsupported(const irept &node, const std::string &what);

  /// The CBMC type of type-table index \p index; reports an error (at
  /// \p where) and returns nil if unsupported.
  typet convert_type(const irep_idt &index, const irept &where);
  /// The type of expression \p expr (its `_t`).
  typet expr_type(const irept &expr);

  // Declarations
  void declare_package_var(const irep_idt &object_id, const irept &where);
  void declare_function(const irept &decl);
  /// Bind the parameters of \p code_type, whose go/types signature is
  /// \p signature, to symbols; unnamed ones get ids below \p function.
  void declare_parameters(
    code_typet &code_type,
    const irept &signature,
    const irep_idt &function,
    const source_locationt &location);
  void convert_function_body(const irept &decl);
  /// Convert \p body into the value of function \p function, whose
  /// go/types signature has type-table index \p signature_index.
  /// \p captured: for function literals, the objects of the variables it
  /// captures, in the order of the fields of its environment.
  void convert_function(
    const irep_idt &function,
    const irep_idt &signature_index,
    const irept &body,
    const source_locationt &location,
    const std::vector<irep_idt> &captured = {});
  /// Collect the variables in \p body that must live on the heap: those
  /// whose address is taken and those captured by a function literal.
  void scan_heap_variables(const irept &body);
  /// A new variable of type \p type with value \p value, which outlives
  /// the current function (Go1.27 §Variables): code allocating it is
  /// appended to \p dest, and the result points to it.
  /// An allocation of a new variable of type \p type (uninitialised).
  exprt allocation(const typet &type, const source_locationt &location);
  symbol_exprt new_heap_variable(
    const typet &type,
    const exprt &value,
    const source_locationt &location,
    code_blockt &dest);
  /// The function value referring to declared function \p function,
  /// through an adapter that takes (and ignores) an environment.
  exprt function_value(const symbolt &function, const source_locationt &);
  void convert_package_init(const std::vector<irep_idt> &init_functions);
  /// Declare the local variable for \p object_id; for heap variables, the
  /// result is the dereferenced pointer to it.
  exprt declare_local(
    const irep_idt &object_id,
    const typet &type,
    const source_locationt &location,
    code_blockt &dest);
  symbol_exprt new_temporary(
    const typet &type,
    const source_locationt &location,
    code_blockt &dest);
  /// The symbol for an object used as a variable.
  exprt variable(const irept &ident);

  // Statements
  code_blockt convert_block(const irept &block);
  void convert_statement(const irept &stmt, code_blockt &dest);
  void convert_decl_stmt(const irept &stmt, code_blockt &dest);
  void convert_assign(const irept &stmt, code_blockt &dest);
  void convert_inc_dec(const irept &stmt, code_blockt &dest);
  /// m[k] op= y, or m[k]++/-- for a nil \p y_node.
  void convert_map_update(
    const irept &target,
    const irep_idt &op,
    const irept &y_node,
    const irept &stmt,
    code_blockt &dest);
  void convert_if(const irept &stmt, code_blockt &dest);
  /// \p label_object: the object id of the label of the statement, if any.
  void convert_for(
    const irept &stmt,
    code_blockt &dest,
    const irep_idt &label_object = irep_idt{});
  void convert_range(
    const irept &stmt,
    code_blockt &dest,
    const irep_idt &label_object = irep_idt{});
  void convert_switch(
    const irept &stmt,
    code_blockt &dest,
    const irep_idt &label_object = irep_idt{});
  void convert_labeled(const irept &stmt, code_blockt &dest);
  void convert_branch(const irept &stmt, code_blockt &dest);
  void convert_return(const irept &stmt, code_blockt &dest);
  void convert_expr_stmt(const irept &stmt, code_blockt &dest);
  /// Calls that are statements only (panic, cprover.Assert, ...): appends
  /// code and returns true; otherwise returns false.
  bool convert_intrinsic_call(const irept &call, code_blockt &dest);

  /// Code evaluating the operands of an access path (pointer
  /// indirections, field selectors and index expressions), and the checks
  /// for its run-time panics (Go1.27 §Run_time_panics).
  struct access_partst
  {
    code_blockt operands;
    code_blockt checks;
    /// Evaluate every non-constant operand into a temporary, as the left
    /// side of an assignment with several targets requires.
    bool force_temporaries = false;
  };
  /// Convert the variable, pointer indirection, field selector or index
  /// expression \p expr, which may be assigned to.
  exprt convert_access(const irept &expr, access_partst &parts);
  /// \p expr converted, evaluated into a temporary in \p parts if needed.
  exprt access_operand(const irept &expr, access_partst &parts);
  exprt access_operand(exprt value, access_partst &parts);
  /// \p value preceded by \p parts, as a statement expression if needed.
  exprt with_parts(exprt value, access_partst &parts);
  /// \p value preceded by \p prefix, as a statement expression if needed.
  static exprt
  with_prefix(exprt value, code_blockt &prefix, const source_locationt &);
  /// Panic unless \p pointer is not nil (Go1.27 §Address_operators).
  void check_not_nil(const exprt &pointer, code_blockt &dest);
  /// Panic unless \p index is in range for \p size elements (Go1.27
  /// §Index_expressions).
  void check_index(
    const exprt &index,
    const mp_integer &size,
    const source_locationt &location,
    code_blockt &dest);

  // Strings and slices (go_builtins.cpp)
  /// The string constant with bytes \p bytes.
  exprt string_constant(const std::string &bytes, const source_locationt &);
  /// Code copying \p count elements from \p source to \p dest (pointers
  /// to elements; the ranges may overlap), appended to \p dest_code.
  void copy_elements(
    const exprt &dest,
    const exprt &source,
    const exprt &count,
    const source_locationt &location,
    code_blockt &dest_code);
  /// A new array of \p count elements of type \p element, zeroed if
  /// \p zero; the result points to its first element.
  exprt allocate_elements(
    const typet &element,
    const exprt &count,
    bool zero,
    const source_locationt &location,
    code_blockt &dest);
  /// Calls of the generated helper functions $string_equal and
  /// $string_concat (see go_builtins.cpp).
  exprt string_equal(const exprt &a, const exprt &b, const source_locationt &);
  exprt string_concat(const exprt &a, const exprt &b, const source_locationt &);
  /// A call of function \p name of the runtime package cprover/rt.
  exprt runtime_call(
    const std::string &name,
    exprt::operandst arguments,
    const source_locationt &location);
  /// An arbitrary value of \p type (strings get arbitrary contents).
  exprt nondet_value(
    const typet &type,
    const source_locationt &location,
    code_blockt &dest);
  /// \p value, or a temporary holding it if it is not a symbol or a
  /// constant (whose declaration and assignment go to \p dest).
  exprt evaluate_once(
    const exprt &value,
    const source_locationt &location,
    code_blockt &dest);
  exprt convert_slice_expr(const irept &expr);
  exprt convert_make(const irept &call);
  exprt convert_append(const irept &call);
  exprt convert_copy(const irept &call);
  void convert_clear(const irept &call, code_blockt &dest);
  /// Conversions involving strings and slices (Go1.27 §Conversions); nil
  /// if \p value and \p type are not such types.
  exprt convert_string_conversion(
    const exprt &value,
    const typet &type,
    const irept &where);
  /// The slice literal \p lit (Go1.27 §Composite_literals).
  exprt convert_slice_lit(const irept &lit, const typet &type);
  /// The element \p index of string or slice \p value: a pointer
  /// dereference, after checks added to \p checks.
  exprt element(
    const exprt &value,
    const exprt &index,
    bool constant_index,
    const source_locationt &location,
    code_blockt &checks);

  // Maps (go_maps.cpp)
  /// The index of the entry of \p map with key \p key, or -1 (also for
  /// nil maps); \p map and \p key have no side effects.
  symbol_exprt map_find(
    const exprt &map,
    const exprt &key,
    const irept &where,
    code_blockt &dest);
  /// The map index expression \p expr, as a pair (value, ok) if
  /// \p comma_ok.
  exprt convert_map_lookup(const irept &expr, bool comma_ok);
  /// Code setting the entry of \p map with key \p key to \p value (none of
  /// which have side effects).
  void map_insert(
    const exprt &map,
    const exprt &key,
    const exprt &value,
    const irept &where,
    code_blockt &dest);
  /// A new, empty map of type \p type.
  exprt
  make_map(const typet &type, const source_locationt &, code_blockt &dest);
  exprt convert_map_lit(const irept &lit, const typet &type);
  void convert_delete(const irept &call, code_blockt &dest);
  void convert_map_range(
    const irept &stmt,
    const exprt &map,
    code_blockt &dest,
    const irep_idt &label_object);

  // Floating-point and complex numbers (go_floats.cpp)
  static bool is_float_like(const typet &type);
  /// The constant \p expr of floating-point or complex type \p type.
  exprt float_constant(const irept &expr, const typet &type);
  exprt float_arithmetic(
    const irep_idt &op,
    exprt x,
    exprt y,
    const typet &type,
    const source_locationt &location);
  /// \p z rounded to the complex type \p type.
  exprt convert_complex(const exprt &z, const typet &type);
  /// == on floating-point or complex values (IEEE 754 comparison).
  static exprt float_equality(const exprt &x, const exprt &y);
  /// Conversions involving floating-point values; nil for other types.
  exprt numeric_conversion(
    const exprt &value,
    const typet &type,
    const source_locationt &location);
  /// min(x, y) or max(x, y) of floating-point values without side effects.
  static exprt float_min_max(const exprt &x, const exprt &y, bool is_min);
  exprt convert_complex_builtin(const irept &call, const irep_idt &callee);

  // Methods and interfaces (go_interfaces.cpp)
  /// The dynamic types: every type converted to an interface, by type id.
  std::map<std::size_t, typet> dynamic_types;
  /// Generated functions calling a method (name, signature type id) of an
  /// interface value.
  std::map<std::pair<irep_idt, std::size_t>, irep_idt> dispatchers;
  /// Generated functions checking that a dynamic type implements the
  /// interface type with the given type id.
  std::map<std::size_t, irep_idt> implements_checks;
  /// The Go type id recorded on \p type (ID_C_go_tid), or 0.
  std::size_t go_type_id(const typet &type) const;
  typet convert_type_of_basic(const std::string &kind);
  /// \p value converted to \p type as for an assignment: go_cast, and
  /// values of non-interface type stored in interface values.
  exprt assign_cast(exprt value, const typet &type);
  /// The interface value holding (a copy of) \p value.
  exprt box(const exprt &value, const source_locationt &location);
  /// The dynamic value of interface value \p iface as a value of \p type.
  static exprt unbox(const exprt &iface, const typet &type);
  irep_idt dispatcher(const irep_idt &name, const irep_idt &signature_index);
  /// Whether the dynamic type with id \p type_id implements the interface
  /// at type-table index \p interface_index.
  exprt implements(const exprt &type_id, const irep_idt &interface_index);
  exprt interface_equal(
    const exprt &x,
    const exprt &y,
    const source_locationt &location);
  /// The receiver argument for calling \p method_object on \p x through the
  /// selection path \p path (an interface value for interface methods).
  exprt method_receiver(
    exprt x,
    const irept &path,
    const irep_idt &method_object,
    const source_locationt &location,
    code_blockt &checks);
  exprt call_method(
    const exprt &receiver,
    const irep_idt &method_object,
    const irep_idt &signature_index,
    exprt::operandst arguments,
    const source_locationt &location);
  exprt convert_method_call(const irept &call);
  exprt method_adapter(
    const irep_idt &id,
    const typet &receiver_type,
    const irep_idt &method_object,
    const irep_idt &signature_index,
    const source_locationt &location);
  exprt convert_method_value(const irept &selector);
  exprt convert_type_assertion(const irept &expr);
  /// Whether the dynamic type of \p iface is (or, for interface types,
  /// implements) the type at \p type_index.
  exprt dynamic_type_is(const exprt &iface, const irep_idt &type_index);
  void convert_type_switch(
    const irept &stmt,
    code_blockt &dest,
    const irep_idt &label_object = irep_idt{});
  bool is_comparable(const typet &type) const;
  /// Generate the bodies of dispatchers, implements checks and interface
  /// comparisons, once all dynamic types are known.
  void finish_interfaces();

  // Expressions
  exprt convert_expr(const irept &expr);
  exprt convert_composite_lit(const irept &lit);
  exprt convert_address_of(const irept &expr);
  exprt convert_new(const irept &call);
  exprt convert_len(const irept &call);
  /// The arithmetic operation \p op of \p x and \p y in \p type, with
  /// the checks for its run-time panics; \p y_node is the right operand.
  exprt convert_arithmetic(
    const irep_idt &op,
    exprt x,
    exprt y,
    const irept &y_node,
    const typet &type,
    const source_locationt &location);
  /// Equality (\p equal) of values of the comparable type of \p x and
  /// \p y, which have no side effects (Go1.27 §Comparison_operators).
  exprt equality(const exprt &x, const exprt &y, const irept &where);
  exprt convert_constant(const irept &expr, const typet &type);
  exprt convert_unary(const irept &expr);
  exprt convert_binary(const irept &expr);
  /// The comparison \p x \p op \p y (Go1.27 §Comparison_operators).
  exprt
  convert_comparison(const irep_idt &op, exprt x, exprt y, const irept &where);
  exprt convert_func_lit(const irept &lit);
  /// The values of a multi-valued expression \p value (a struct, see
  /// go_type_convertert::tuple), evaluated once into a temporary.
  std::vector<exprt> unpack(
    const exprt &value,
    const source_locationt &location,
    code_blockt &dest);
  exprt convert_call(const irept &call);
  /// The arguments of \p call, converted to the parameter types of
  /// \p code_type, including f(g()) with multi-valued g (Go1.27 §Calls).
  bool convert_arguments(
    const irept &call,
    const code_typet &code_type,
    exprt::operandst &arguments,
    code_blockt &prefix);
  exprt convert_conversion(const irept &call);
  exprt convert_min_max(const irept &call, bool is_min);
  /// \p value, evaluated only if \p condition holds; otherwise the program
  /// panics (Go1.27 §Run_time_panics), see add_panic.
  exprt guard_panic(
    exprt value,
    const exprt &condition,
    const std::string &property_class,
    const std::string &comment,
    const source_locationt &location);
  /// Code for a run-time panic with message \p comment unless \p condition
  /// holds (Go1.27 §Run_time_panics), see raise_panic.
  void add_panic(
    code_blockt &dest,
    const exprt &condition,
    const std::string &property_class,
    const std::string &comment,
    const source_locationt &location);

  // Panics, deferred calls and recover (go_panics.cpp)
  /// Code starting to panic with interface value \p value (Go1.27
  /// §Handling_panics): it records the value and the panic site and jumps
  /// to the end of the function (label `$unwind`), from where the
  /// deferred calls run and the function returns; callers continue
  /// unwinding after the call (add_panic_checks). A panic that reaches the
  /// top-level function is reported as a failing property of class
  /// \p property_class at \p location (see finish_panics).
  void raise_panic(
    code_blockt &dest,
    const exprt &value,
    const std::string &property_class,
    const std::string &comment,
    const source_locationt &location);
  /// The interface value of a run-time panic with message \p message.
  exprt runtime_error(const std::string &message, const source_locationt &);
  /// Declare the variables recording the panic state.
  void declare_panic_state();
  /// The panic state: whether the goroutine is panicking, the value
  /// passed to panic, the panic site, and whether the function now called
  /// is a deferred one (for recover).
  static symbol_exprt panicking();
  static symbol_exprt panic_value();
  static symbol_exprt panic_site();
  static symbol_exprt defer_token();
  /// After each call in \p code (to a Go function), continue unwinding if
  /// the callee panicked.
  void add_panic_checks(exprt &code);
  /// Set up deferred calls and recover for the function whose body is
  /// \p body_node; appended to \p dest (the start of the function).
  void begin_function_panics(const irept &body_node, code_blockt &dest);
  /// The end of a function: label `$unwind`, the deferred calls, and the
  /// return, appended to \p body.
  void end_function_panics(code_blockt &body);
  /// The end of a generated function that may panic, returning \p type.
  static void end_generated_function(code_blockt &body, const typet &type);
  /// The type of the nodes of the list of deferred calls.
  static struct_typet deferred_node_type();
  void convert_defer(const irept &stmt, code_blockt &dest);
  exprt convert_recover(const irept &call);
  /// The function `go::$check_panics`, called after the top-level
  /// function, with a property per panic site.
  void finish_panics();

  /// Object id of the function a call refers to, or empty.
  irep_idt callee_object(const irept &call);
};

/// Declare the generated function \p id with parameters of types
/// \p parameters and result type \p result (body added later).
void declare_generated(
  symbol_table_baset &symbol_table,
  const irep_idt &id,
  const std::vector<typet> &parameters,
  const typet &result);
/// The parameters of \p function, as symbol expressions.
std::vector<exprt> parameter_symbols(const symbolt &function);

/// \p expr without enclosing parentheses and explicit instantiations
/// (`f[int]`, `x.m[int]`): the instantiated function or method, whose node
/// denotes the instance.
const irept &skip_instantiation(const irept &expr);

#endif // CPROVER_GO_GO_CONVERT_H
