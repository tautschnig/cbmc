/// \file
/// Python to GOTO converter — converts Python JSON AST to CBMC symbol table

#ifndef CPROVER_PYTHON_PYTHON_CONVERTER_H
#define CPROVER_PYTHON_PYTHON_CONVERTER_H

#include <util/bitvector_types.h>
#include <util/mathematical_types.h>
#include <util/message.h>
#include <util/namespace.h>
#include <util/std_code.h>
#include <util/symbol_table_base.h>

#include "python_parse_tree.h"

#include <map>
#include <set>

/// Converts a Python JSON AST into CBMC's symbol table representation.
/// This populates the symbol table with function symbols and their bodies
/// as codet trees, which are then converted to goto programs by the
/// standard goto_convert pipeline.
class python_convertert
{
public:
  python_convertert(
    symbol_table_baset &_symbol_table,
    const python_parse_treet &_parse_tree,
    message_handlert &_message_handler);

  /// Main entry point: convert the entire parse tree.
  /// \return true on error
  bool convert();

  /// Enable mathematical (unbounded) integers instead of int64.
  void set_unbounded_ints(bool v)
  {
    unbounded_ints = v;
  }

private:
  symbol_table_baset &symbol_table;
  const python_parse_treet &parse_tree;
  messaget log;
  std::string filename;

  /// Current function name (empty for top-level code)
  std::string current_function;

  /// Current class name (empty when not inside a class method)
  std::string current_class;

  /// Names declared 'global' in the current function
  std::set<std::string> global_names;

  /// Whether to use mathematical integers instead of int64.
  bool unbounded_ints = false;

  /// Map from class name to its struct type
  std::map<std::string, struct_typet> class_types;
  std::map<std::string, int> class_tag_ids;

  /// Map from class name to its base class names (for isinstance)
  std::map<std::string, std::vector<std::string>> class_bases;

  /// Map from variable name to function symbol (for lambda assignments)
  std::map<std::string, irep_idt> function_aliases;

  /// Known imported module names (for `import math` style)
  std::set<std::string> imported_modules;
  std::set<std::string> imported_math_funcs;
  std::set<std::string> generator_functions;

  /// Map from variable name (qualified) to its current versioned symbol.
  /// Used for fresh variable renaming when a variable changes type.
  std::map<std::string, irep_idt> variable_versions;

  /// Counter for generating unique version suffixes.
  std::map<std::string, unsigned> version_counters;

  /// Depth of if/else nesting (>0 means we're inside a branch).
  unsigned if_else_depth = 0;
  unsigned try_depth = 0;

  // --- AST node converters ---

  /// Convert a top-level module body into a code_blockt.
  code_blockt convert_module_body(const jsont &body);

  /// Convert a single statement node.
  codet convert_statement(const jsont &stmt);

  /// Convert an expression node to an exprt.
  exprt convert_expression(const jsont &expr);

  /// Convert a type annotation to a CBMC typet.
  typet convert_type_annotation(const jsont &annotation);

  // --- Statement converters ---
  codet convert_assign(const jsont &stmt);
  codet convert_ann_assign(const jsont &stmt);
  codet convert_aug_assign(const jsont &stmt);
  codet convert_assert(const jsont &stmt);

  /// Pending checks (div-by-zero, bounds) to be emitted before the
  /// next statement. Populated by expression converters, consumed by
  /// statement converters.
  std::vector<codet> pending_checks;
  codet convert_if(const jsont &stmt);
  codet convert_while(const jsont &stmt);
  codet convert_for(const jsont &stmt);
  codet convert_return(const jsont &stmt);
  codet convert_function_def(const jsont &stmt);
  codet convert_class_def(const jsont &stmt);
  codet convert_expr_stmt(const jsont &stmt);
  codet convert_break();
  codet convert_continue();
  codet convert_pass();
  codet convert_raise(const jsont &stmt);
  codet convert_with(const jsont &stmt);
  codet convert_try(const jsont &stmt);

  // --- Expression converters ---
  exprt convert_name(const jsont &expr);
  exprt convert_constant(const jsont &expr);
  exprt convert_bin_op(const jsont &expr);
  exprt convert_unary_op(const jsont &expr);
  exprt convert_bool_op(const jsont &expr);
  exprt convert_compare(const jsont &expr);
  exprt convert_call(const jsont &expr);
  exprt convert_if_exp(const jsont &expr);
  exprt convert_subscript(const jsont &expr);
  exprt convert_tuple(const jsont &expr);
  exprt convert_list(const jsont &expr);
  exprt convert_attribute(const jsont &expr);
  exprt convert_dict(const jsont &expr);
  exprt convert_list_comp(const jsont &expr);
  exprt convert_lambda(const jsont &expr);

  /// Counter for generating unique lambda names.
  unsigned lambda_counter = 0;

  // --- Helpers ---

  /// Get the source location from a JSON AST node.
  source_locationt get_location(const jsont &node) const;

  /// Get a JSON object member, returning null_json if not found.
  const jsont &json_member(const jsont &obj, const std::string &key) const;

  /// Get a string value from a JSON node.
  std::string json_string(const jsont &node) const;

  /// Get an integer value from a JSON node.
  long long json_integer(const jsont &node) const;

  /// Check if a JSON node has a specific _type field.
  bool is_node_type(const jsont &node, const std::string &type_name) const;

  /// Safely cast a jsont to json_arrayt, returning an empty array if not array.
  const json_arrayt &as_array(const jsont &node) const;

  /// Add a property check (assertion) to pending_checks.
  void add_check(
    exprt condition,
    const std::string &property_class,
    const std::string &comment,
    const source_locationt &loc);

  /// Get the qualified symbol name for a variable, respecting
  /// function scope and 'global' declarations.
  std::string qualify_name(const std::string &name) const;

  /// Return the CBMC type used for Python int.
  typet python_int_type() const
  {
    if(unbounded_ints)
      return integer_typet{};
    return signedbv_typet{64};
  }

  /// Unwrap a tagged-union value to a specific type, or return it as-is
  /// if it's already a concrete type.
  exprt unwrap_value(const exprt &e, const typet &target_type) const;

  /// Wrap a concrete typed value into a tagged-union value.
  exprt wrap_value(const exprt &e);

  /// Safe typecast: handles tagged unions, struct-to-scalar, and other
  /// cases that would crash with a raw typecast_exprt.
  exprt safe_typecast(const exprt &e, const typet &target);

  /// Safe zero: returns from_integer(0, type) for numeric types,
  /// or a nondet value for struct/other types.
  exprt safe_zero(const typet &type) const;

  /// Compute exception type hash for a given type name.
  /// Uses class_tag_ids if available, else sum of ASCII values.
  long exception_type_hash(const std::string &type_name) const;
};

#endif // CPROVER_PYTHON_PYTHON_CONVERTER_H
