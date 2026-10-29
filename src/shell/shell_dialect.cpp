/// \file
/// Shell dialects supported by the shell front-end.

#include "shell_dialect.h"

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

/// The interpreter named by a `#!` line: the last path component, or the
/// first argument when the interpreter is `env`.
std::string interpreter(const std::string &source)
{
  if(source.compare(0, 2, "#!") != 0)
    return {};
  const std::string line = source.substr(2, source.find('\n') - 2);
  std::size_t pos = 0;
  auto next_word = [&]() -> std::string
  {
    while(pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
      ++pos;
    const std::size_t start = pos;
    while(pos < line.size() && line[pos] != ' ' && line[pos] != '\t' &&
          line[pos] != '\r')
    {
      ++pos;
    }
    std::string word = line.substr(start, pos - start);
    const std::size_t slash = word.rfind('/');
    return slash == std::string::npos ? word : word.substr(slash + 1);
  };
  std::string word = next_word();
  if(word == "env")
  {
    word = next_word();
    while(!word.empty() && word[0] == '-') // e.g. env -S
      word = next_word();
  }
  return word;
}
} // namespace

shell_dialectt
detect_shell_dialect(const std::string &source, const std::string &path)
{
  const std::string program = interpreter(source);
  if(program == "bash")
    return shell_dialectt::BASH;
  if(program == "zsh")
    return shell_dialectt::ZSH;
  if(program == "mksh" || program == "ksh" || program == "ksh93")
    return shell_dialectt::MKSH;
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
    break;
  }
  return options;
}
