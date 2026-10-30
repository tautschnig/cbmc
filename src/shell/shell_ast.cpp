/// \file
/// Printing of the shell IR (used by --show-parse-tree).

#include "shell_ast.h"

#include "shell_arith.h"

#include <ostream>

namespace
{
/// Print a string in double quotes with C-like escapes, so that the
/// output stays on one line and is unambiguous.
void print_string(std::ostream &out, const std::string &s)
{
  out << '"';
  for(unsigned char c : s)
  {
    switch(c)
    {
    case '"':
      out << "\\\"";
      break;
    case '\\':
      out << "\\\\";
      break;
    case '\n':
      out << "\\n";
      break;
    case '\t':
      out << "\\t";
      break;
    default:
      if(c < 0x20 || c == 0x7f)
      {
        static const char hex[] = "0123456789abcdef";
        out << "\\x" << hex[c >> 4] << hex[c & 0xf];
      }
      else
        out << c;
    }
  }
  out << '"';
}

void print_location(std::ostream &out, const source_locationt &location)
{
  if(!location.get_line().empty())
  {
    out << " @" << location.get_line();
    if(!location.get_column().empty())
      out << ':' << location.get_column();
  }
}

class printert
{
public:
  printert(std::ostream &out, const shell_arith_optionst &arith_options)
    : out(out), arith_options(arith_options)
  {
  }

  void print_list(const shell_command_listt &list, unsigned indent)
  {
    for(const auto &command : list)
      print_command(command, indent);
  }

private:
  std::ostream &out;
  const shell_arith_optionst arith_options;

  /// Command substitutions found while printing a word on one line.
  /// They are printed as numbered references and expanded below.
  std::vector<const shell_command_listt *> pending;
  /// Number of command substitutions printed so far; they are numbered
  /// uniquely across the program.
  std::size_t substitution_count = 0;

  void line(unsigned indent)
  {
    out << std::string(2 * indent, ' ');
  }

  void print_part(const shell_word_partt &part)
  {
    using kindt = shell_word_partt::kindt;
    switch(part.kind)
    {
    case kindt::LITERAL:
      out << "lit";
      print_string(out, part.text);
      break;
    case kindt::QUOTED_LITERAL:
      out << "q";
      print_string(out, part.text);
      break;
    case kindt::DOUBLE_QUOTED:
      out << "dq(";
      print_parts(part.parts);
      out << ')';
      break;
    case kindt::PARAMETER:
      out << "param(";
      if(part.parameter_length)
        out << '#';
      out << part.text;
      if(!part.parameter_operator.empty())
      {
        out << ' ' << part.parameter_operator << ' ';
        print_parts(part.parts);
      }
      out << ')';
      break;
    case kindt::COMMAND_SUBSTITUTION:
      pending.push_back(part.commands.get());
      out << "cmdsubst#" << substitution_count + pending.size();
      break;
    case kindt::ARITHMETIC:
    {
      // Show the raw text and how the native arithmetic parser reads it
      // with the dialect's rules (XCU 2.6.4).
      out << "arith";
      print_string(out, part.text);
      auto parsed = parse_shell_arithmetic(part.text, arith_options);
      if(parsed.expression.has_value())
        out << "{" << to_string(*parsed.expression) << "}";
      else
        out << "{error: " << parsed.error << "}";
      break;
    }
    }
  }

  void print_parts(const std::vector<shell_word_partt> &parts)
  {
    bool first = true;
    for(const auto &part : parts)
    {
      if(!first)
        out << ' ';
      first = false;
      print_part(part);
    }
  }

  /// Print a word on the current line, then any command substitutions
  /// it contains as indented blocks below it.
  void print_word_line(
    const std::string &label,
    const shell_wordt &word,
    unsigned indent)
  {
    line(indent);
    out << label << ' ';
    pending.clear();
    print_parts(word);
    out << '\n';
    const auto substitutions = pending;
    const std::size_t first = substitution_count + 1;
    substitution_count += substitutions.size();
    for(std::size_t i = 0; i < substitutions.size(); ++i)
    {
      line(indent + 1);
      out << "cmdsubst#" << (first + i) << ":\n";
      if(substitutions[i] != nullptr)
        print_list(*substitutions[i], indent + 2);
    }
  }

  void print_block(
    const std::string &label,
    const shell_command_listt &list,
    unsigned indent)
  {
    line(indent);
    out << label << ":\n";
    print_list(list, indent + 1);
  }

  static const char *kind_name(shell_commandt::kindt kind)
  {
    using kindt = shell_commandt::kindt;
    switch(kind)
    {
    case kindt::SIMPLE:
      return "simple";
    case kindt::PIPELINE:
      return "pipeline";
    case kindt::AND:
      return "and";
    case kindt::OR:
      return "or";
    case kindt::SUBSHELL:
      return "subshell";
    case kindt::BRACE_GROUP:
      return "group";
    case kindt::IF:
      return "if";
    case kindt::WHILE:
      return "while";
    case kindt::UNTIL:
      return "until";
    case kindt::FOR:
      return "for";
    case kindt::CASE:
      return "case";
    case kindt::FUNCTION_DEFINITION:
      return "function";
    }
    return "?";
  }

  void print_command(const shell_commandt &command, unsigned indent)
  {
    using kindt = shell_commandt::kindt;
    line(indent);
    out << kind_name(command.kind);
    if(command.kind == kindt::FOR || command.kind == kindt::FUNCTION_DEFINITION)
    {
      out << ' ' << command.name;
    }
    if(command.negated)
      out << " negated";
    if(command.background)
      out << " background";
    print_location(out, command.location);
    out << '\n';

    for(const auto &assignment : command.assignments)
      print_word_line(
        "assign " + assignment.name + " =", assignment.value, indent + 1);
    for(const auto &word : command.words)
      print_word_line("word", word, indent + 1);

    switch(command.kind)
    {
    case kindt::SIMPLE:
      break;
    case kindt::PIPELINE:
    case kindt::AND:
    case kindt::OR:
    case kindt::SUBSHELL:
    case kindt::BRACE_GROUP:
    case kindt::FUNCTION_DEFINITION:
      print_list(command.body, indent + 1);
      break;
    case kindt::IF:
      print_block("condition", command.condition, indent + 1);
      print_block("then", command.body, indent + 1);
      if(!command.else_body.empty())
        print_block("else", command.else_body, indent + 1);
      break;
    case kindt::WHILE:
    case kindt::UNTIL:
      print_block("condition", command.condition, indent + 1);
      print_block("do", command.body, indent + 1);
      break;
    case kindt::FOR:
      if(command.has_in)
      {
        line(indent + 1);
        out << "in:\n";
        for(const auto &word : command.for_items)
          print_word_line("word", word, indent + 2);
      }
      print_block("do", command.body, indent + 1);
      break;
    case kindt::CASE:
      print_word_line("subject", command.case_word, indent + 1);
      for(const auto &item : command.case_items)
      {
        line(indent + 1);
        out << "item " << item.terminator;
        print_location(out, item.location);
        out << '\n';
        for(const auto &pattern : item.patterns)
          print_word_line("pattern", pattern, indent + 2);
        print_block("body", item.body, indent + 2);
      }
      break;
    }

    for(const auto &redirect : command.redirects)
    {
      std::string label = "redirect ";
      if(redirect.fd >= 0)
        label += std::to_string(redirect.fd);
      label += redirect.op;
      print_word_line(label, redirect.target, indent + 1);
      if(!redirect.here_document.empty())
        print_word_line("here-document", redirect.here_document, indent + 2);
    }
  }
};
} // namespace

void shell_programt::output(std::ostream &out) const
{
  out << "program " << filename << '\n';
  out << "dialect " << shfmt_dialect_name(dialect) << '\n';
  printert{out, arith_options(dialect)}.print_list(commands, 1);
}
