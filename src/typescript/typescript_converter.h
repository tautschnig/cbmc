/// \file
/// TypeScript converter class - converts TypeScript JSON AST to GOTO symbols
/// GOTO converter - header

#ifndef CPROVER_TYPESCRIPT_TYPESCRIPT_CONVERTER_H
#define CPROVER_TYPESCRIPT_TYPESCRIPT_CONVERTER_H

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/json.h>
#include <util/message.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/std_types.h>
#include <util/symbol_table_base.h>

#include <json/json_parser.h>

#include <map>
#include <set>

/// Maximum array size for TypeScript arrays (elements beyond this are truncated)
#define TYPESCRIPT_DEFAULT_MAX_ARRAY_LENGTH 16

/// Converts a TypeScript JSON AST (produced by ts_ast_to_json.js)
/// into GOTO program symbols in the symbol table.
class typescript_convertert
{
public:
  bool bounds_check = false;
  bool div_by_zero_check = false;
  bool nan_check = false;
  bool integer_inference = true; // auto-detect integer variables
  std::size_t TYPESCRIPT_MAX_ARRAY_LENGTH = TYPESCRIPT_DEFAULT_MAX_ARRAY_LENGTH;

  typescript_convertert(
    symbol_table_baset &_symbol_table,
    const std::string &_filename,
    const jsont &_ast_json,
    message_handlert &_message_handler)
    : symbol_table(_symbol_table),
      filename(_filename),
      ast_json(_ast_json),
      log(_message_handler)
  {
  }

  /// Main entry point: convert the entire source file.
  bool convert();

  /// Convert a type string (e.g., "number", "string[]") to a CBMC typet.
  typet convert_type(const std::string &ts_type) const;

private:
  symbol_table_baset &symbol_table;
  std::string filename;
  const jsont &ast_json;
  messaget log;

  std::string current_function;
  std::string current_class; // empty at module level
  std::map<irep_idt, std::string> string_constants;
  mutable std::map<std::string, typet>
    type_cache; // cache for convert_type results
  std::map<std::string, std::string> parent_class; // child -> parent
  std::set<irep_idt> rest_param_functions;
  // Captured variables: func_id -> [(param_name, outer_symbol_id)]
  std::map<irep_idt, std::vector<std::pair<std::string, irep_idt>>>
    captured_var_map; // functions with rest params
  // Closure bindings: variable → {function_id, bound_values}
  // When a function returns a closure, the call site records the binding.
  struct closure_bindingt
  {
    irep_idt function_id;
    exprt::operandst bound_values;
  };
  std::map<irep_idt, closure_bindingt> closure_bindings;
  std::map<std::string, struct_typet> class_types;
  std::vector<codet> pending_stmts;
  // Default parameter values: func_id → {param_index → default_expr}
  std::map<irep_idt, std::map<std::size_t, exprt>>
    default_values; // stmts to emit before current expr

  // Integer inference: variables that can safely use integer types
  enum class num_kindt
  {
    FLOAT,   // default: floatbv[64]
    INDEX,   // array index / .length / loop counter: signedbv[64]
    INTEGER, // general integer (%, integer literals): signedbv[32]
  };
  std::map<std::string, num_kindt> inferred_num_kind;
  void infer_integer_types(const jsont &statements);
  typet number_type_for(const std::string &var_name) const;

  // --- Helpers ---
  static const jsont &json_member(const jsont &obj, const std::string &key);
  static std::string json_string(const jsont &val);
  static bool is_kind(const jsont &node, const std::string &kind);
  source_locationt get_location(const jsont &node) const;

  /// Build the standard typescript_array struct type for a given data array type.
  static struct_typet make_array_struct_type(const array_typet &data_type)
  {
    struct_typet list_type;
    list_type.components().push_back(
      struct_typet::componentt{"length", signedbv_typet{64}});
    list_type.components().push_back(
      struct_typet::componentt{"data", data_type});
    list_type.set_tag("typescript_array");
    return list_type;
  }

  // --- Expression conversion ---
  exprt convert_expression(const jsont &node);
  exprt convert_binary_expression(const jsont &node);
  exprt convert_prefix_unary_expression(const jsont &node);
  exprt convert_call_expression(const jsont &node);
  exprt convert_identifier(const jsont &node);
  exprt convert_numeric_literal(const jsont &node);
  exprt convert_string_literal(const jsont &node);
  exprt convert_string_literal_from_text(const std::string &text);
  /// Extract constant string value from an expression.
  /// Returns the string prefixed with "S:" if found, empty string otherwise.
  std::string extract_string_value(const exprt &e);
  exprt convert_boolean_literal(const jsont &node);

  // --- Statement conversion ---
  codet convert_statement(const jsont &node);
  codet convert_variable_statement(const jsont &node);
  codet convert_expression_statement(const jsont &node);
  codet convert_if_statement(const jsont &node);
  codet convert_while_statement(const jsont &node);
  codet convert_for_statement(const jsont &node);
  codet convert_return_statement(const jsont &node);
  codet convert_block(const jsont &node);

  // --- Function conversion ---
  void convert_function_declaration(const jsont &node);
  void convert_function_declaration_with_name(
    const jsont &node,
    const std::string &name);

  // --- Module body ---
  void convert_module_body(const jsont &statements);
};

#endif // CPROVER_TYPESCRIPT_TYPESCRIPT_CONVERTER_H
