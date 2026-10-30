/// \file
/// Shell dialects supported by the shell front-end.

#include "shell_dialect.h"

#include <langapi/mode.h>

#include <cctype>
#include <vector>

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

std::optional<std::string> shebang_options(const std::string &source)
{
  if(source.compare(0, 2, "#!") != 0)
    return std::string{};
  std::string line = source.substr(2, source.find('\n') - 2);
  if(!line.empty() && line.back() == '\r')
    line.pop_back();
  std::vector<std::string> words;
  std::size_t pos = 0;
  while(pos < line.size())
  {
    while(pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
      ++pos;
    const std::size_t start = pos;
    while(pos < line.size() && line[pos] != ' ' && line[pos] != '\t')
      ++pos;
    if(pos > start)
      words.push_back(line.substr(start, pos - start));
  }
  if(words.empty())
    return std::string{};

  // Linux passes the rest of the line after the interpreter as one
  // argument; env -S splits it into several.
  std::vector<std::string> arguments{words.begin() + 1, words.end()};
  const std::string interpreter = words[0].substr(
    words[0].rfind('/') == std::string::npos ? 0 : words[0].rfind('/') + 1);
  if(interpreter == "env")
  {
    if(arguments.empty())
      return std::string{};
    if(arguments[0].compare(0, 2, "-S") != 0)
    {
      // env gets one argument, the name of the shell.
      if(arguments.size() > 1)
        return {};
      return std::string{};
    }
    if(arguments[0] != "-S")
      arguments[0] = arguments[0].substr(2);
    else
      arguments.erase(arguments.begin());
    // Skip further env options and assignments, then the shell's name.
    auto it = arguments.begin();
    while(it != arguments.end() &&
          ((*it)[0] == '-' || it->find('=') != std::string::npos))
    {
      ++it;
    }
    if(it == arguments.end())
      return {};
    arguments.erase(arguments.begin(), it + 1);
  }
  else if(arguments.size() > 1)
    return {};

  std::string options;
  for(const auto &argument : arguments)
  {
    if(argument == "-" || argument == "--")
      continue;
    if(argument.size() < 2 || argument[0] != '-')
      return {};
    for(std::size_t i = 1; i < argument.size(); ++i)
    {
      if(!std::isalpha(static_cast<unsigned char>(argument[i])))
        return {};
      options += argument[i];
    }
  }
  return options;
}
