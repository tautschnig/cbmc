/*******************************************************************\

Module: JML Expression Parser

Author: Kiro (AI agent)

\*******************************************************************/

/// \file
/// Recursive-descent parser for JML (Java Modeling Language)
/// expressions. Produces CBMC exprt trees from tokenized JML text.
///
/// Grammar (simplified, JML Level 0):
///
///   expr          ::= equiv_expr
///   equiv_expr    ::= implies_expr (('<==>' | '<=!=>') implies_expr)*
///   implies_expr  ::= or_expr ('==>' implies_expr)?   // right-assoc
///   or_expr       ::= and_expr ('||' and_expr)*
///   and_expr      ::= bitor_expr ('&&' bitor_expr)*
///   bitor_expr    ::= xor_expr ('|' xor_expr)*
///   xor_expr      ::= bitand_expr ('^' bitand_expr)*
///   bitand_expr   ::= equality_expr ('&' equality_expr)*
///   equality_expr ::= relational_expr (('==' | '!=') relational_expr)*
///   relational_expr ::= shift_expr (('<' | '>' | '<=' | '>=' | 'instanceof') shift_expr)?
///   shift_expr    ::= additive_expr (('<<' | '>>' | '>>>') additive_expr)*
///   additive_expr ::= mult_expr (('+' | '-') mult_expr)*
///   mult_expr     ::= unary_expr (('*' | '/' | '%') unary_expr)*
///   unary_expr    ::= ('!' | '~' | '-' | '+') unary_expr
///                    | postfix_expr
///   postfix_expr  ::= primary ('.' IDENT | '.' IDENT '(' args ')' | '[' expr ']')*
///   primary       ::= INTEGER_LITERAL | BOOLEAN_LITERAL | NULL_LITERAL
///                    | STRING_LITERAL | IDENTIFIER | 'this' | 'super'
///                    | '\result' | '\old' '(' expr ')'
///                    | '\forall' type IDENT ';' expr ';' expr
///                    | '\exists' type IDENT ';' expr ';' expr
///                    | '\fresh' '(' expr ')'
///                    | '\typeof' '(' expr ')'
///                    | '\nonnullelements' '(' expr ')'
///                    | '(' type ')' unary_expr   // cast
///                    | '(' expr ')'              // grouping
///                    | '(' expr '?' expr ':' expr ')'  // ternary

#ifndef CPROVER_JAVA_BYTECODE_JML_JML_PARSER_H
#define CPROVER_JAVA_BYTECODE_JML_JML_PARSER_H

#include <util/expr.h>
#include <util/message.h>
#include <util/symbol_table_base.h>

#include <string>

/// Result of parsing a JML expression.
struct jml_parse_resultt
{
  /// The parsed expression tree, or nil_exprt() on failure.
  exprt expr;

  /// True if parsing succeeded.
  bool success = false;

  /// Error message if parsing failed.
  std::string error_message;

  /// Position in the input where the error occurred.
  std::size_t error_position = 0;
};

/// Parse a JML expression string into a CBMC exprt tree.
///
/// The expression is parsed in the context of a specific method
/// (for resolving \result, parameter names, field names). The
/// symbol_table provides type information for field accesses and
/// method calls.
///
/// \param jml_text: The JML expression text (without annotation
///   markers like /*@ or @*/).
/// \param method_id: The fully-qualified identifier of the method
///   this expression belongs to (for \result resolution).
/// \param class_id: The fully-qualified class identifier (for
///   field resolution).
/// \param symbol_table: The global symbol table for type lookups.
/// \return A jml_parse_resultt with the expression tree or error.
jml_parse_resultt jml_parse_expression(
  const std::string &jml_text,
  const irep_idt &method_id,
  const irep_idt &class_id,
  const symbol_table_baset &symbol_table);

/// Parse a JML clause (requires, ensures, assignable, etc.).
/// Returns the clause kind and the parsed expression.
struct jml_clauset
{
  enum class kindt
  {
    REQUIRES,
    ENSURES,
    ASSIGNABLE,
    SIGNALS,
    INVARIANT,
    DECREASES,
    PURE,
    NULLABLE,
    NON_NULL,
    ALSO,
    UNKNOWN
  };

  kindt kind = kindt::UNKNOWN;
  exprt expr;           // The clause's expression (nil for PURE, etc.)
  std::string raw_text; // Original text for diagnostics
};

/// Parse a single JML clause line (e.g., "requires x > 0;").
jml_clauset jml_parse_clause(
  const std::string &clause_text,
  const irep_idt &method_id,
  const irep_idt &class_id,
  const symbol_table_baset &symbol_table);

#endif // CPROVER_JAVA_BYTECODE_JML_JML_PARSER_H
