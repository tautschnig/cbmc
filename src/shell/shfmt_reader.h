/// \file
/// Conversion of the typed JSON syntax tree produced by
/// `shfmt --to-json` (https://github.com/mvdan/sh) into the shell IR.

#ifndef CPROVER_SHELL_SHFMT_READER_H
#define CPROVER_SHELL_SHFMT_READER_H

#include "shell_ast.h"
#include "shell_dialect.h"

#include <string>

class jsont;
class message_handlert;
class messaget;

/// Convert the JSON output of `shfmt --to-json` into \p dest.
/// \param json: the parsed JSON document
/// \param source: the exact bytes given to shfmt, used to recover the
///   raw text of arithmetic expansions from byte offsets
/// \param filename: used in source locations
/// \param [out] dest: the program
/// \param message_handler: errors are reported here, with locations
/// \return true on error. Every construct outside the supported subset
///   is reported as an error rather than dropped.
bool shfmt_json_to_shell_program(
  const jsont &json,
  const std::string &source,
  const std::string &filename,
  shell_programt &dest,
  message_handlert &message_handler);

/// Decode the contents of a dollar-single-quoted string, per POSIX.1-2024
/// XCU 2.2.4 Dollar-Single-Quotes. \p raw is the text between "$'" and
/// the terminating "'". Escape sequences whose results the standard
/// leaves unspecified produce a warning and are kept literally.
std::string decode_dollar_single_quotes(
  const std::string &raw,
  const source_locationt &location,
  messaget &log);

#endif // CPROVER_SHELL_SHFMT_READER_H
