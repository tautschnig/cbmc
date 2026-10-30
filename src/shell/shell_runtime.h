/// \file
/// Runtime support for converted shell scripts: a bounded string type and
/// helper functions, written in C and added to the symbol table.

#ifndef CPROVER_SHELL_SHELL_RUNTIME_H
#define CPROVER_SHELL_SHELL_RUNTIME_H

#include <util/cprover_prefix.h>
#include <util/irep.h>

#include <cstddef>
#include <iosfwd>
#include <set>
#include <string>

class message_handlert;
class symbol_table_baset;

/// Struct tag of the string type. Its components are `set` (_Bool: the
/// variable is set), `length` (unsigned long long), and `data` (unsigned
/// char[max_length]); bytes at and after `length` are unspecified.
#define SHELL_STRING_TAG CPROVER_PREFIX "shell_string"

/// Names of the runtime functions; see \ref shell_runtime_source.
#define SHELL_RT_NONDET_STRING CPROVER_PREFIX "shell_nondet_string"
#define SHELL_RT_ENVIRONMENT CPROVER_PREFIX "shell_environment"
#define SHELL_RT_EQUAL CPROVER_PREFIX "shell_equal"
#define SHELL_RT_CONCAT CPROVER_PREFIX "shell_concat"
#define SHELL_RT_FROM_INTEGER CPROVER_PREFIX "shell_from_integer"
#define SHELL_RT_TEST_INTEGER CPROVER_PREFIX "shell_test_integer"
#define SHELL_RT_ARITH_INTEGER CPROVER_PREFIX "shell_arith_integer"
#define SHELL_RT_SUBSTRING CPROVER_PREFIX "shell_substring"
#define SHELL_RT_HAS_PREFIX CPROVER_PREFIX "shell_has_prefix"
#define SHELL_RT_HAS_SUFFIX CPROVER_PREFIX "shell_has_suffix"

/// The C source of the runtime for strings of at most \p max_length bytes.
/// The functions are written without loops (they are unrolled here), so
/// that the --unwind bound chosen for the script does not cut them short.
std::string shell_runtime_source(std::size_t max_length);

/// The string type and the prototypes of the runtime, for further C code
/// that uses them.
std::string shell_runtime_declarations(std::size_t max_length);

/// Start and end the body of a runtime function \p signature: steps are
/// hidden in traces, and checks requested for the script are disabled.
void shell_runtime_function_begin(
  std::ostream &out,
  const std::string &signature);
void shell_runtime_function_end(std::ostream &out);

/// Parse and typecheck further runtime code \p source, which defines the
/// functions \p functions, and add it to \p symbol_table.
/// \return true on error
bool add_shell_runtime_code(
  const std::string &source,
  const std::set<irep_idt> &functions,
  symbol_table_baset &symbol_table,
  message_handlert &message_handler);

/// The names that must survive symbol table cleanup after the runtime is
/// added: all of the functions above and the string type.
std::set<irep_idt> shell_runtime_symbols();

/// Parse and typecheck the runtime for \p max_length and add it to
/// \p symbol_table, unless it is already there.
/// \return true on error
bool add_shell_runtime(
  std::size_t max_length,
  symbol_table_baset &symbol_table,
  message_handlert &message_handler);

#endif // CPROVER_SHELL_SHELL_RUNTIME_H
