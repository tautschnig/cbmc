/// \file
/// Parsing shell scripts with shfmt (https://github.com/mvdan/sh).

#ifndef CPROVER_SHELL_SHFMT_PARSER_H
#define CPROVER_SHELL_SHFMT_PARSER_H

#include "shell_ast.h"

#include <string>

class message_handlert;

/// Parse \p source, the contents of the file \p path, into \p program, in
/// the dialect that \p program already has. Runs $CBMC_SHFMT, or shfmt on
/// the PATH.
/// \return true on error
bool parse_shell_source(
  const std::string &source,
  const std::string &path,
  shell_programt &program,
  message_handlert &message_handler);

#endif // CPROVER_SHELL_SHFMT_PARSER_H
