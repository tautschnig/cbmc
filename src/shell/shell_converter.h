/// \file
/// Conversion of the shell IR into symbols and code for CBMC.

#ifndef CPROVER_SHELL_SHELL_CONVERTER_H
#define CPROVER_SHELL_SHELL_CONVERTER_H

#include <util/cprover_prefix.h>
#include <util/irep.h>

#include <goto-programs/dynamic_sites.h>

#include <cstddef>

class message_handlert;
class shell_programt;
class symbol_table_baset;

/// The mode of symbols created for shell scripts.
const irep_idt &shell_mode();

/// Identifier of the function holding the script's top-level commands.
#define SHELL_MAIN_FUNCTION "shell::main"

/// Default for --shell-max-string-length.
#define SHELL_DEFAULT_MAX_STRING_LENGTH 64

/// Default for --shell-max-arguments.
#define SHELL_DEFAULT_MAX_ARGUMENTS 8

/// Struct tag of shell values: `set` (_Bool), `value`, a refined string or
/// an SMT-LIB string, and `is_int` and `int_value` for strings that are the
/// decimal representation of an integer.
#define SHELL_VALUE_TAG CPROVER_PREFIX "shell_value"

/// How shell strings are represented (--shell-strings).
enum class shell_string_representationt
{
  /// Unbounded strings for the string refinement solver (--refine-strings):
  /// `cprover_string_*` function applications over refined strings.
  REFINED,
  /// Unbounded strings of the SMT-LIB String sort, for --z3 and --cvc5.
  NATIVE
};

/// Bounds of the model. Inputs (the environment, arguments, command
/// output) are assumed to respect them; values built by the script that
/// would exceed them fail a property.
struct shell_convert_optionst
{
  /// With refined strings, inputs are strings of at most this many bytes.
  std::size_t max_string_length = SHELL_DEFAULT_MAX_STRING_LENGTH;
  /// There are at most this many positional parameters, and lists of
  /// fields (such as the arguments of a function call) have at most this
  /// many elements.
  std::size_t max_arguments = SHELL_DEFAULT_MAX_ARGUMENTS;
  shell_string_representationt strings = shell_string_representationt::REFINED;
  /// The candidates of dynamic sites (--dynamic-resolutions).
  dynamic_resolutionst resolutions;
};

/// Convert \p program into `shell::main` and the variable symbols it uses,
/// and fill `__CPROVER_initialize` with the initial state: variables from
/// the environment, positional parameters, and special parameters.
///
/// Constructs outside the supported subset are errors rather than being
/// ignored, so that a script never appears to be verified when parts of it
/// were not modelled. See doc/architectural/shell-frontend-plan.md.
/// \return true on error
bool shell_convert(
  const shell_programt &program,
  const shell_convert_optionst &options,
  symbol_table_baset &symbol_table,
  message_handlert &message_handler);

#endif // CPROVER_SHELL_SHELL_CONVERTER_H
