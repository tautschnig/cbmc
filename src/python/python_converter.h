/// \file
/// Python to GOTO converter — converts Python JSON AST to CBMC symbol table

#ifndef CPROVER_PYTHON_PYTHON_CONVERTER_H
#define CPROVER_PYTHON_PYTHON_CONVERTER_H

#include <util/message.h>
#include <util/namespace.h>
#include <util/std_code.h>
#include <util/symbol_table_base.h>

#include "python_parse_tree.h"

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

private:
  symbol_table_baset &symbol_table;
  const python_parse_treet &parse_tree;
  messaget log;
  std::string filename;

  /// Current function name (empty for top-level code)
  std::string current_function;

  /// Current class name (empty when not inside a class method)
  std::string current_class;

  /// Map from class name to its struct type
  std::map<std::string, struct_typet> class_types;

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
};

#endif // CPROVER_PYTHON_PYTHON_CONVERTER_H
