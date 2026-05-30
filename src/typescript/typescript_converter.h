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
  bool integer_inference =
    false; // off by default; enable via --ts-integer-mode
  bool async_threading =
    false; // off by default; enable via --ts-async-threading
  bool bigint_mathematical =
    false; // off by default; enable via --ts-bigint-mathematical
  unsigned solver_string_alloc_count = 0;
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
  // Frozen symbols: symbols for which Object.freeze was called.
  // Queried by Object.isFrozen and used to suppress property writes.
  std::set<irep_idt> frozen_symbols;
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
  // Generator function name -> list of yield value expressions
  // (constants captured at conversion time). Used by `yield*`
  // delegation in another generator's body.
  std::map<std::string, std::vector<exprt>> generator_yields;
  // Private fields: class_name → set of private field names
  std::map<std::string, std::set<std::string>> private_fields;
  std::vector<codet> pending_stmts;
  // Generic functions: name → AST node (deferred for monomorphization)
  std::map<std::string, jsont> generic_functions;
  // Generic classes: name → AST node (deferred for monomorphization)
  std::map<std::string, jsont> generic_classes;
  // Current generic instantiation context
  std::string current_generic_type_param;
  std::string current_generic_concrete;
  // Multi-parameter generic instantiation: each type parameter name
  // maps to its concrete type. Populated at call sites that pass
  // multiple type arguments (e.g. pair<number, string>(1, "x")).
  std::map<std::string, std::string> current_generic_type_map;
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

  /// Build a refined_string_exprt suitable for passing to a
  /// cprover_string_*_func. The input is one of our TypeScript-
  /// string-typed values (inline-array struct shape); we copy its
  /// length and data into scalar-typed temporaries, emit the
  /// array <-> pointer and length <-> array associations into
  /// `pending_stmts`, and return a refined struct whose children
  /// are all scalar-typed (so the solver's recursive walker doesn't
  /// trip over our refined-string-typed originals).
  ///
  /// See doc/refined-string-migration-plan.md for context.
  exprt ts_string_to_refined(const exprt &ts_string);

  /// Call a cprover_string_*_func that RETURNS a string, then
  /// repack the result into our inline-array struct.
  ///
  /// The solver signature is:
  ///   int func(result_length, result_content, args...)
  /// where result_length and result_content are fresh scalar temps
  /// the solver constrains to describe the result string. We
  /// allocate those temps, emit the call, then build our struct
  /// via per-slot cprover_string_char_at_func calls.
  ///
  /// Used for concat, toLowerCase, toUpperCase, trim, substring,
  /// repeat, padStart, padEnd, etc.
  exprt ts_call_string_returning_function(
    const irep_idt &func_id,
    const exprt::operandst &args);

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
  /// ES2024 §7.1.2 ToBoolean coercion. For a TypeScript string
  /// receiver, returns `length > 0`. For bool, returns as-is. For
  /// other types, falls back to a plain typecast (which is correct
  /// for numeric types — non-zero is truthy).
  exprt ts_to_boolean(const exprt &e);
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
