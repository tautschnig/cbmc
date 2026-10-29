/// \file
/// Shell dialects supported by the shell front-end.

#include "shell_dialect.h"

#include <langapi/mode.h>

std::string shfmt_dialect_name(shell_dialectt dialect)
{
  switch(dialect)
  {
  case shell_dialectt::POSIX:
    return "posix";
  case shell_dialectt::BASH:
    return "bash";
  case shell_dialectt::MKSH:
    return "mksh";
  case shell_dialectt::ZSH:
    return "zsh";
  }
  return "posix";
}

namespace
{
bool ends_with(const std::string &s, const std::string &suffix)
{
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}
} // namespace

shell_dialectt
detect_shell_dialect(const std::string &source, const std::string &path)
{
  const std::string program = shebang_interpreter(source);
  if(program == "bash")
    return shell_dialectt::BASH;
  if(program == "zsh")
    return shell_dialectt::ZSH;
  if(
    program == "mksh" || program == "ksh" || program == "ksh93" ||
    program == "lksh" || program == "pdksh")
  {
    return shell_dialectt::MKSH;
  }
  if(!program.empty())
    return shell_dialectt::POSIX; // sh, dash, ash, yash, posh, ...

  if(ends_with(path, ".bash"))
    return shell_dialectt::BASH;
  if(ends_with(path, ".zsh"))
    return shell_dialectt::ZSH;
  if(ends_with(path, ".ksh") || ends_with(path, ".mksh"))
    return shell_dialectt::MKSH;
  return shell_dialectt::POSIX;
}

shell_arith_optionst arith_options(shell_dialectt dialect)
{
  shell_arith_optionst options;
  switch(dialect)
  {
  case shell_dialectt::POSIX:
    // Neither increment operators nor the comma operator are required by
    // POSIX (XCU 2.6.4, 1.1.2.1); follow dash, the common /bin/sh.
    options.increment_mode = shell_arith_increment_modet::NONE;
    options.comma_operator = false;
    break;
  case shell_dialectt::BASH:
    options.increment_mode = shell_arith_increment_modet::LENIENT;
    options.comma_operator = true;
    break;
  case shell_dialectt::MKSH:
  case shell_dialectt::ZSH:
    options.increment_mode = shell_arith_increment_modet::STRICT;
    options.comma_operator = true;
    options.leading_zero_octal = false;
    break;
  }
  return options;
}
