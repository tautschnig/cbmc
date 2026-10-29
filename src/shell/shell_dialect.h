/// \file
/// Shell dialects supported by the shell front-end.

#ifndef CPROVER_SHELL_SHELL_DIALECT_H
#define CPROVER_SHELL_SHELL_DIALECT_H

#include "shell_arith.h"

#include <string>

/// Shell dialects. POSIX means POSIX.1-2024 (XCU chapter 2); the others
/// are deltas documented in the respective reference manuals.
enum class shell_dialectt
{
  POSIX,
  BASH,
  MKSH,
  ZSH
};

/// The value of shfmt's `-ln` option for \p dialect.
std::string shfmt_dialect_name(shell_dialectt dialect);

/// Choose a dialect from the script's `#!` line, falling back to the
/// file name extension, and finally to POSIX.
shell_dialectt
detect_shell_dialect(const std::string &source, const std::string &path);

/// The arithmetic expression syntax (XCU 2.6.4 plus extensions) of
/// \p dialect.
shell_arith_optionst arith_options(shell_dialectt dialect);

#endif // CPROVER_SHELL_SHELL_DIALECT_H
