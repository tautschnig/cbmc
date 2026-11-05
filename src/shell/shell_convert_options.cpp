/// \file
/// Shell converter: shell options (POSIX.1-2024 XCU 2.15 set).
///
/// errexit (-e), nounset (-u), noglob (-f), and pipefail are modelled as
/// variables shell::opt::NAME, set from the #! line and by set. Options
/// that only affect output, interactive use, or what is passed to other
/// programs are accepted and have no effect. noclobber (-C) needs nothing:
/// redirections that open files may fail anyway.

#include "shell_convert_class.h"

#include <util/config.h>

namespace
{
enum class option_effectt
{
  MODELLED,
  NO_EFFECT
};

struct option_infot
{
  std::string name;
  option_effectt effect;
};
} // namespace

/// The option for letter \p letter in \p dialect, if supported.
static std::optional<option_infot>
option_for_letter(char letter, shell_dialectt dialect)
{
  // zsh's single-letter options differ (-f is NO_RCS, for example); only
  // accept those that mean the same.
  if(
    dialect == shell_dialectt::ZSH &&
    std::string{"euxv"}.find(letter) == std::string::npos)
  {
    return {};
  }
  switch(letter)
  {
  case 'e':
    return option_infot{"errexit", option_effectt::MODELLED};
  case 'u':
    return option_infot{"nounset", option_effectt::MODELLED};
  case 'f':
    return option_infot{"noglob", option_effectt::MODELLED};
  case 'x':
    return option_infot{"xtrace", option_effectt::NO_EFFECT};
  case 'v':
    return option_infot{"verbose", option_effectt::NO_EFFECT};
  case 'C':
    return option_infot{"noclobber", option_effectt::NO_EFFECT};
  case 'a':
    return option_infot{"allexport", option_effectt::NO_EFFECT};
  case 'b':
    return option_infot{"notify", option_effectt::NO_EFFECT};
  case 'h':
    return option_infot{"hashall", option_effectt::NO_EFFECT};
  case 'm':
    return option_infot{"monitor", option_effectt::NO_EFFECT};
  default:
    return {};
  }
}

/// The option for long name \p name (set -o name), if supported.
static std::optional<option_infot>
option_for_name(const std::string &name, shell_dialectt dialect)
{
  if(name == "pipefail")
    return option_infot{name, option_effectt::MODELLED};
  if(name == "ignoreeof" || name == "nolog" || name == "vi" || name == "emacs")
  {
    return option_infot{name, option_effectt::NO_EFFECT};
  }
  for(char letter : std::string{"eufxvCabhm"})
  {
    auto option = option_for_letter(letter, shell_dialectt::POSIX);
    if(option.has_value() && option->name == name)
    {
      if(
        dialect == shell_dialectt::ZSH &&
        !option_for_letter(letter, dialect).has_value() && name != "noglob" &&
        name != "pipefail")
      {
        return {};
      }
      return option;
    }
  }
  return {};
}

symbol_exprt shell_convertert::option(const std::string &name)
{
  return global_symbol("shell::opt::" + name, name, bool_typet{});
}

/// Set or clear option \p name; \return false if it is not supported.
bool shell_convertert::set_option(
  const std::string &name,
  bool on,
  code_blockt &dest,
  const source_locationt &loc)
{
  if(
    name == "errexit" || name == "nounset" || name == "noglob" ||
    name == "pipefail")
  {
    assign(
      dest, option(name), on ? exprt{true_exprt{}} : exprt{false_exprt{}}, loc);
  }
  return true;
}

void shell_convertert::check_option_possible(
  const std::string &name,
  bool on,
  const source_locationt &loc)
{
  // The effects of options are only modelled where collect_options found
  // that they may be enabled, as with set reached by a dynamic command
  // name, it may not.
  if(on && !option_possible(name))
    unsupported(loc, "set enabling " + name + " where it is not expected");
}

std::vector<shell_wordt>::const_iterator shell_convertert::convert_set_option(
  std::vector<shell_wordt>::const_iterator arg,
  std::vector<shell_wordt>::const_iterator end,
  code_blockt &dest,
  const source_locationt &loc)
{
  const std::string text = *static_text(*arg);
  const bool on = text[0] == '-';
  ++arg;
  if(text.size() == 1)
    unsupported(loc, "set " + text);
  for(std::size_t i = 1; i < text.size(); ++i)
  {
    if(text[i] == 'o')
    {
      // -o name, or -o alone, which lists the options.
      if(arg == end)
        continue;
      const auto name = static_text(*arg);
      if(!name.has_value())
        unsupported(loc, "set -o with an expanded name");
      ++arg;
      const auto option = option_for_name(*name, program.dialect);
      if(
        !option.has_value() && *name == "igncr" &&
        program.dialect == shell_dialectt::BASH &&
        config.shell.platform != configt::shellt::platformt::CYGWIN)
      {
        // bash has igncr on Cygwin only. Elsewhere set reports the
        // invalid name, and stops with status 2.
        invalid_set_option = true;
        return end;
      }
      if(!option.has_value())
        unsupported(loc, "set option " + *name);
      check_option_possible(option->name, on, loc);
      set_option(option->name, on, dest, loc);
      continue;
    }
    const auto option = option_for_letter(text[i], program.dialect);
    if(!option.has_value())
      unsupported(loc, std::string{"set option -"} + text[i]);
    check_option_possible(option->name, on, loc);
    set_option(option->name, on, dest, loc);
  }
  return arg;
}

/// Record which options may be enabled anywhere in the script, so that
/// their effects are only modelled where they can matter.
void shell_convertert::collect_options(const shell_command_listt &list)
{
  for(const auto &command : list)
  {
    if(command.kind == shell_commandt::kindt::SIMPLE && !command.words.empty())
    {
      const auto name = static_text(command.words.front());
      if(name.has_value() && *name == "set")
      {
        // Options end at --, - or the first operand (XCU 2.15 set);
        // later words are positional parameters.
        for(std::size_t i = 1; i < command.words.size(); ++i)
        {
          const auto text = static_text(command.words[i]);
          if(
            !text.has_value() || text->size() < 2 || *text == "--" ||
            ((*text)[0] != '-' && (*text)[0] != '+'))
          {
            break;
          }
          // As in convert_set_option: each o takes the next word as the
          // name of an option, and + disables options.
          const bool on = (*text)[0] == '-';
          std::size_t names = 0;
          for(char letter : text->substr(1))
          {
            if(letter == 'o')
            {
              ++names;
              continue;
            }
            const auto option = option_for_letter(letter, program.dialect);
            if(on && option.has_value())
              possible_options.insert(option->name);
          }
          for(; names > 0 && i + 1 < command.words.size(); --names)
          {
            ++i;
            const auto next = static_text(command.words[i]);
            if(on && next.has_value())
              possible_options.insert(*next);
          }
        }
      }
    }
    for(const auto *sub :
        {&command.body, &command.condition, &command.else_body})
    {
      collect_options(*sub);
    }
    for(const auto &item : command.case_items)
      collect_options(item.body);
    auto words = command.words;
    for(const auto &assignment : command.assignments)
      words.push_back(assignment.value);
    for(const auto &word : words)
    {
      for(const auto &part : word)
        collect_options(part);
    }
  }
}

void shell_convertert::collect_options(const shell_word_partt &part)
{
  if(part.commands != nullptr)
    collect_options(*part.commands);
  for(const auto &inner : part.parts)
    collect_options(inner);
}

void shell_convertert::initialize_options(
  code_blockt &init,
  const source_locationt &loc)
{
  for(const std::string name : {"errexit", "nounset", "noglob", "pipefail"})
    set_option(name, false, init, loc);
  for(char letter : program.initial_options)
  {
    const auto option = option_for_letter(letter, program.dialect);
    if(!option.has_value())
    {
      unsupported(loc, std::string{"option -"} + letter + " on the #! line");
    }
    set_option(option->name, true, init, loc);
  }
}

bool shell_convertert::option_possible(const std::string &name) const
{
  if(possible_options.count(name) != 0)
    return true;
  for(char letter : program.initial_options)
  {
    const auto option = option_for_letter(letter, program.dialect);
    if(option.has_value() && option->name == name)
      return true;
  }
  return false;
}

void shell_convertert::check_errexit(
  code_blockt &dest,
  const source_locationt &loc)
{
  if(errexit_ignored || !option_possible("errexit"))
    return;
  // -e: exit when a command fails, except where -e is ignored
  // (XCU 2.15 set).
  code_blockt leave;
  exit_shell(leave, false, loc);
  add(
    dest,
    code_ifthenelset{
      and_exprt{
        option("errexit"),
        not_exprt{errexit_ignored_in_caller()},
        notequal_exprt{status(), from_integer(0, status_type())}},
      std::move(leave)},
    loc);
}

void shell_convertert::check_nounset(
  const exprt &parameter,
  const std::string &name,
  code_blockt &dest,
  const source_locationt &loc)
{
  if(!option_possible("nounset"))
    return;
  // -u: expanding an unset parameter other than @ or * is an error, which
  // makes a non-interactive shell exit (XCU 2.8.1).
  code_blockt error;
  exit_shell(error, true, loc);
  add(
    dest,
    code_ifthenelset{
      and_exprt{option("nounset"), not_exprt{is_set(parameter)}},
      std::move(error)},
    loc);
  (void)name;
}
