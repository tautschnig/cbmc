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
#include <util/namespace.h>
#include <util/std_code.h>
#include <util/std_expr.h>

#include "go_types.h"

class go_parse_treet;
class symbol_table_baset;
class symbolt;

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

  /// The CBMC symbol for the Go object with id \p object_id.
  /// Package-level objects ("main.f") become "go::main.f"; local objects,
  /// whose ids carry a uniqueness suffix ("main.x#3"), become
  /// "go::main::3::x", so that symbol names end in their base name.
  static irep_idt symbol_id(const irep_idt &object_id)
  {
    const std::string id = id2string(object_id);
    const auto hash = id.rfind('#');
    if(hash == std::string::npos)
      return "go::" + id;
    const std::string qualified = id.substr(0, hash); // pkg.name
    const auto dot = qualified.rfind('.');            // pkg may contain dots
    return "go::" + qualified.substr(0, dot) + "::" + id.substr(hash + 1) +
           "::" + qualified.substr(dot + 1);
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
    std::vector<symbol_exprt> named_results;
    std::size_t tmp_counter = 0;
    std::size_t label_counter = 0;
    std::size_t literal_counter = 0;
    std::vector<jump_targett> jump_targets;
  };
  function_statet state;

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
  void convert_function(
    const irep_idt &function,
    const irep_idt &signature_index,
    const irept &body,
    const source_locationt &location);
  void convert_package_init(const std::vector<irep_idt> &init_functions);
  symbol_exprt declare_local(
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
  void convert_if(const irept &stmt, code_blockt &dest);
  /// \p label_object: the object id of the label of the statement, if any.
  void convert_for(
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

  // Expressions
  exprt convert_expr(const irept &expr);
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
  /// panics (Go1.27 §Run_time_panics), modelled as a failing property of
  /// class \p property_class, after which execution does not continue.
  exprt guard_panic(
    exprt value,
    const exprt &condition,
    const std::string &property_class,
    const std::string &comment,
    const source_locationt &location);
  /// Code for a panic with message \p comment: a failing property and the
  /// end of the execution (no recover yet: Go1.27 §Handling_panics).
  void add_panic(
    code_blockt &dest,
    const exprt &condition,
    const std::string &property_class,
    const std::string &comment,
    const source_locationt &location);

  /// Object id of the function a call refers to, or empty.
  irep_idt callee_object(const irept &call);
};

#endif // CPROVER_GO_GO_CONVERT_H
