/// \file
/// TypeScript to GOTO converter — header

#ifndef CPROVER_TYPESCRIPT_TYPESCRIPT_CONVERTER_H
#define CPROVER_TYPESCRIPT_TYPESCRIPT_CONVERTER_H

#include <util/message.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/symbol_table_base.h>

#include <json/json_parser.h>
#include <map>
#include <set>
#include <util/json.h>

/// Converts a TypeScript JSON AST (produced by ts_ast_to_json.js)
/// into GOTO program symbols in the symbol table.
class typescript_convertert
{
public:
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

private:
  symbol_table_baset &symbol_table;
  std::string filename;
  const jsont &ast_json;
  messaget log;

  std::string current_function;
  std::string current_class; // empty at module level
  std::map<irep_idt, std::string> string_constants;
  std::set<irep_idt> rest_param_functions; // functions with rest params
  std::map<std::string, struct_typet> class_types;
  std::vector<codet> pending_stmts;
  // Default parameter values: func_id → {param_index → default_expr}
  std::map<irep_idt, std::map<std::size_t, exprt>> default_values; // stmts to emit before current expr

  // --- Helpers ---
  static const jsont &json_member(const jsont &obj, const std::string &key);
  static std::string json_string(const jsont &val);
  static bool is_kind(const jsont &node, const std::string &kind);
  source_locationt get_location(const jsont &node) const;

  // --- Type conversion ---
  // ES2024 sec-ecmascript-language-types: maps TS type strings to CBMC types
  typet convert_type(const std::string &ts_type) const;

  // --- Expression conversion ---
  exprt convert_expression(const jsont &node);
  exprt convert_binary_expression(const jsont &node);
  exprt convert_prefix_unary_expression(const jsont &node);
  exprt convert_call_expression(const jsont &node);
  exprt convert_identifier(const jsont &node);
  exprt convert_numeric_literal(const jsont &node);
  exprt convert_string_literal(const jsont &node);
  exprt convert_string_literal_from_text(const std::string &text);
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
