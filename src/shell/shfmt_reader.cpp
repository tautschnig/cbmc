/// \file
/// Conversion of shfmt's typed JSON syntax tree into the shell IR.
///
/// The node and field names follow mvdan/sh's syntax/nodes.go. Fields
/// with default values (false, empty, nil) are omitted from the JSON.

#include "shfmt_reader.h"

#include <util/json.h>
#include <util/message.h>
#include <util/string2int.h>

#include <cctype>
#include <set>

namespace
{
bool is_octal(char c)
{
  return c >= '0' && c <= '7';
}

int hex_value(char c)
{
  if(c >= '0' && c <= '9')
    return c - '0';
  if(c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if(c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}
} // namespace

std::string decode_dollar_single_quotes(
  const std::string &raw,
  const source_locationt &location,
  messaget &log)
{
  std::string result;
  auto unspecified = [&](const std::string &what)
  {
    log.warning().source_location = location;
    log.warning() << "$'...' " << what
                  << " has unspecified results (POSIX.1-2024 XCU 2.2.4); "
                     "keeping it literally"
                  << messaget::eom;
  };

  for(std::size_t i = 0; i < raw.size(); ++i)
  {
    if(raw[i] != '\\' || i + 1 == raw.size())
    {
      result += raw[i];
      continue;
    }

    const char c = raw[++i];
    switch(c)
    {
    case '"':
      result += '"';
      break;
    case '\'':
      result += '\'';
      break;
    case '\\':
      result += '\\';
      break;
    case 'a':
      result += '\a';
      break;
    case 'b':
      result += '\b';
      break;
    case 'e':
      result += '\x1b';
      break;
    case 'f':
      result += '\f';
      break;
    case 'n':
      result += '\n';
      break;
    case 'r':
      result += '\r';
      break;
    case 't':
      result += '\t';
      break;
    case 'v':
      result += '\v';
      break;
    case 'c':
    {
      // \cX: the control character for X in the ^c column of the stty
      // "Circumflex Control Characters" table (letters, [ \ ] ^ _ ?);
      // \c\\ yields <FS>.
      if(i + 1 == raw.size())
      {
        unspecified("\\c at the end");
        result += "\\c";
        break;
      }
      char x = raw[++i];
      if(x == '\\' && i + 1 < raw.size() && raw[i + 1] == '\\')
      {
        ++i;
        result += '\x1c';
      }
      else if(x == '?')
        result += '\x7f';
      else if(std::isalpha(static_cast<unsigned char>(x)))
        result += static_cast<char>(std::toupper(x) & 0x1f);
      else if(x == '[' || x == ']' || x == '^' || x == '_')
        result += static_cast<char>(x & 0x1f);
      else
      {
        unspecified(std::string{"\\c"} + x);
        result += "\\c";
        result += x;
      }
      break;
    }
    case 'x':
    {
      std::size_t digits = 0;
      unsigned value = 0;
      while(i + 1 < raw.size() && hex_value(raw[i + 1]) >= 0)
      {
        value = value * 16 + static_cast<unsigned>(hex_value(raw[++i]));
        ++digits;
      }
      if(digits == 0 || digits > 2 || value == 0)
      {
        unspecified(
          digits == 0  ? "\\x without hexadecimal digits"
          : digits > 2 ? "\\x with more than two hexadecimal digits"
                       : "\\x yielding a null byte");
        result += "\\x" + raw.substr(i + 1 - digits, digits);
      }
      else
        result += static_cast<char>(value);
      break;
    }
    default:
      if(is_octal(c))
      {
        // \ddd: one to three octal digits.
        unsigned value = static_cast<unsigned>(c - '0');
        std::size_t digits = 1;
        while(digits < 3 && i + 1 < raw.size() && is_octal(raw[i + 1]))
        {
          value = value * 8 + static_cast<unsigned>(raw[++i] - '0');
          ++digits;
        }
        if(value == 0 || value > 0xff)
        {
          unspecified(
            value == 0 ? "\\ddd yielding a null byte"
                       : "\\ddd not fitting in a byte");
          result += '\\' + raw.substr(i + 1 - digits, digits);
        }
        else
          result += static_cast<char>(value);
      }
      else
      {
        unspecified(std::string{"\\"} + c);
        result += '\\';
        result += c;
      }
    }
  }
  return result;
}

namespace
{
class shfmt_readert
{
public:
  shfmt_readert(
    const std::string &source,
    const std::string &filename,
    message_handlert &message_handler)
    : source(source), filename(filename), log(message_handler)
  {
  }

  bool read(const jsont &file, shell_programt &dest)
  {
    dest.filename = filename;
    dest.commands = read_statements(file["Stmts"]);
    return error_found;
  }

private:
  const std::string &source;
  const std::string &filename;
  messaget log;
  bool error_found = false;

  source_locationt location(const jsont &node) const
  {
    source_locationt result;
    result.set_file(filename);
    const jsont &pos = node["Pos"];
    if(pos.is_object())
    {
      result.set_line(pos["Line"].value);
      result.set_column(pos["Col"].value);
    }
    return result;
  }

  void unsupported(const jsont &node, const std::string &what)
  {
    log.error().source_location = location(node);
    log.error() << "unsupported shell construct: " << what << messaget::eom;
    error_found = true;
  }

  static const json_arrayt &array(const jsont &node)
  {
    static const json_arrayt empty;
    return node.is_array() ? to_json_array(node) : empty;
  }

  static bool flag(const jsont &node, const std::string &key)
  {
    return node[key].is_true();
  }

  static std::string type(const jsont &node)
  {
    return node["Type"].value;
  }

  /// Reject any of the given fields if set: they are dialect extensions
  /// the IR cannot represent yet.
  void reject_fields(
    const jsont &node,
    std::initializer_list<std::pair<const char *, const char *>> fields)
  {
    for(const auto &field : fields)
    {
      const jsont &value = node[field.first];
      if(!value.is_null() && !value.is_false())
        unsupported(node, field.second);
    }
  }

  shell_command_listt read_statements(const jsont &statements)
  {
    shell_command_listt result;
    for(const auto &statement : array(statements))
      result.push_back(read_statement(statement));
    return result;
  }

  shell_commandt read_statement(const jsont &statement)
  {
    reject_fields(
      statement,
      {{"Coprocess", "mksh coprocess (|&)"},
       {"Disown", "zsh disown (&| or &!)"}});

    shell_commandt command;
    const jsont &cmd = statement["Cmd"];
    if(cmd.is_object())
      command = read_command(cmd);
    else
    {
      // A statement consisting only of redirections, e.g. `> file`.
      command.kind = shell_commandt::kindt::SIMPLE;
    }
    command.location = location(statement);
    command.negated = flag(statement, "Negated");
    command.background = flag(statement, "Background");
    for(const auto &redirect : array(statement["Redirs"]))
    {
      // bash and zsh: &>word and &>>word are >word 2>&1 and >>word 2>&1.
      const std::string &op = redirect["Op"].value;
      if((op == "&>" || op == "&>>") && !redirect["N"].is_object())
      {
        shell_redirectt output = read_redirect(redirect, op.substr(1));
        shell_redirectt error = output;
        error.op = ">&";
        error.fd = 2;
        shell_word_partt one;
        one.kind = shell_word_partt::kindt::LITERAL;
        one.text = "1";
        one.location = output.location;
        error.target = {one};
        command.redirects.push_back(std::move(output));
        command.redirects.push_back(std::move(error));
        continue;
      }
      command.redirects.push_back(read_redirect(redirect, op));
    }
    return command;
  }

  shell_commandt read_command(const jsont &cmd)
  {
    using kindt = shell_commandt::kindt;
    shell_commandt command;
    command.location = location(cmd);
    const std::string t = type(cmd);

    if(t == "CallExpr")
    {
      command.kind = kindt::SIMPLE;
      for(const auto &assign : array(cmd["Assigns"]))
        command.assignments.push_back(read_assignment(assign));
      for(const auto &arg : array(cmd["Args"]))
        command.words.push_back(read_word(arg));
    }
    else if(t == "BinaryCmd")
    {
      const std::string op = cmd["Op"].value;
      if(op == "&&" || op == "||")
      {
        command.kind = op == "&&" ? kindt::AND : kindt::OR;
        command.body.push_back(read_statement(cmd["X"]));
        command.body.push_back(read_statement(cmd["Y"]));
      }
      else if(op == "|")
      {
        command.kind = kindt::PIPELINE;
        flatten_pipeline(cmd["X"], command.body);
        flatten_pipeline(cmd["Y"], command.body);
      }
      else
        unsupported(cmd, "binary command operator '" + op + "'");
    }
    else if(t == "Subshell" || t == "Block")
    {
      command.kind = t == "Subshell" ? kindt::SUBSHELL : kindt::BRACE_GROUP;
      command.body = read_statements(cmd["Stmts"]);
    }
    else if(t == "IfClause")
      read_if(cmd, command);
    else if(t == "WhileClause")
    {
      command.kind = flag(cmd, "Until") ? kindt::UNTIL : kindt::WHILE;
      command.condition = read_statements(cmd["Cond"]);
      command.body = read_statements(cmd["Do"]);
    }
    else if(t == "ForClause")
      read_for(cmd, command);
    else if(t == "CaseClause")
      read_case(cmd, command);
    else if(t == "FuncDecl")
    {
      if(!array(cmd["Names"]).empty() || !array(cmd["Args"]).empty())
        unsupported(cmd, "zsh function declaration form");
      command.kind = kindt::FUNCTION_DEFINITION;
      command.name = cmd["Name"]["Value"].value;
      command.body.push_back(read_statement(cmd["Body"]));
    }
    else if(t == "DeclClause")
      read_declaration(cmd, command);
    else
      unsupported(cmd, describe_extension(t));

    return command;
  }

  /// In the bash, mksh, and zsh dialects, shfmt parses export, readonly,
  /// local, declare, typeset, and nameref as a DeclClause. Rebuild the
  /// simple command that the POSIX dialect produces for the same text
  /// (words "export", "A=1", ...), so that later stages apply the
  /// declaration utility rules of XCU 2.9.1.1 by command name, whichever
  /// dialect the script was parsed in.
  void read_declaration(const jsont &cmd, shell_commandt &command)
  {
    using kindt = shell_word_partt::kindt;
    command.kind = shell_commandt::kindt::SIMPLE;

    shell_word_partt variant;
    variant.kind = kindt::LITERAL;
    variant.text = cmd["Variant"]["Value"].value;
    variant.location = location(cmd["Variant"]);
    command.words.push_back({std::move(variant)});

    for(const auto &arg : array(cmd["Args"]))
    {
      reject_fields(
        arg,
        {{"Append", "+= assignment (bash/mksh/zsh)"},
         {"Index", "array element assignment (bash/mksh/zsh)"},
         {"Array", "array assignment (bash/mksh/zsh)"}});

      const bool naked = flag(arg, "Naked");
      const jsont &name = arg["Name"];
      shell_wordt word;
      if(name.is_object())
      {
        // "name" or "name=value"; the value is read below.
        shell_word_partt part;
        part.kind = kindt::LITERAL;
        part.text = name["Value"].value + (naked ? "" : "=");
        part.location = location(name);
        word.push_back(std::move(part));
      }
      if(arg["Value"].is_object())
      {
        for(auto &part : read_word(arg["Value"]))
        {
          // Merge adjacent literals, as shfmt does for "A=1" in POSIX mode.
          if(
            part.kind == kindt::LITERAL && !word.empty() &&
            word.back().kind == kindt::LITERAL)
          {
            word.back().text += part.text;
          }
          else
            word.push_back(std::move(part));
        }
      }
      command.words.push_back(std::move(word));
    }
  }

  static std::string describe_extension(const std::string &t)
  {
    if(t == "TestClause")
      return "[[ ... ]] test (bash/mksh/zsh)";
    if(t == "ArithmCmd")
      return "(( ... )) arithmetic command (bash/mksh/zsh)";
    if(t == "LetClause")
      return "let (bash/mksh/zsh)";
    if(t == "TimeClause")
      return "time keyword (bash/mksh/zsh)";
    if(t == "CoprocClause")
      return "coproc (bash)";
    if(t == "TestDecl")
      return "@test declaration (bats)";
    return "command of type " + (t.empty() ? std::string{"<unknown>"} : t);
  }

  /// Append the commands of a pipeline operand. shfmt nests pipelines
  /// as binary commands; POSIX pipelines (XCU 2.9.2) are flat, so we
  /// flatten operands that are themselves unadorned pipelines.
  void flatten_pipeline(const jsont &statement, shell_command_listt &dest)
  {
    const jsont &cmd = statement["Cmd"];
    if(
      cmd.is_object() && type(cmd) == "BinaryCmd" && cmd["Op"].value == "|" &&
      !flag(statement, "Negated") && !flag(statement, "Background") &&
      array(statement["Redirs"]).empty())
    {
      flatten_pipeline(cmd["X"], dest);
      flatten_pipeline(cmd["Y"], dest);
    }
    else
      dest.push_back(read_statement(statement));
  }

  void read_if(const jsont &cmd, shell_commandt &command)
  {
    command.kind = shell_commandt::kindt::IF;
    command.condition = read_statements(cmd["Cond"]);
    command.body = read_statements(cmd["Then"]);
    const jsont &else_clause = cmd["Else"];
    if(!else_clause.is_object())
      return;
    if(array(else_clause["Cond"]).empty())
    {
      // A plain `else`.
      command.else_body = read_statements(else_clause["Then"]);
    }
    else
    {
      // An `elif`: represented as a nested IF.
      shell_commandt elif;
      elif.location = location(else_clause);
      read_if(else_clause, elif);
      command.else_body.push_back(std::move(elif));
    }
  }

  void read_for(const jsont &cmd, shell_commandt &command)
  {
    command.kind = shell_commandt::kindt::FOR;
    reject_fields(
      cmd,
      {{"Select", "select loop (bash/mksh/zsh)"},
       {"Braces", "for loop with braces (bash/mksh)"}});
    const jsont &loop = cmd["Loop"];
    if(type(loop) != "WordIter")
    {
      unsupported(cmd, "C-style for loop (bash/mksh/zsh)");
      return;
    }
    if(!array(loop["Names"]).empty())
      unsupported(cmd, "for loop over several names (zsh)");
    command.name = loop["Name"]["Value"].value;
    // `for name; do` iterates over "$@"; `for name in; do` over nothing.
    const jsont &in_pos = loop["InPos"];
    command.has_in = in_pos.is_object() && in_pos["Line"].value != "0" &&
                     !in_pos["Line"].value.empty();
    for(const auto &item : array(loop["Items"]))
      command.for_items.push_back(read_word(item));
    command.body = read_statements(cmd["Do"]);
  }

  void read_case(const jsont &cmd, shell_commandt &command)
  {
    command.kind = shell_commandt::kindt::CASE;
    reject_fields(cmd, {{"Braces", "case with braces (mksh)"}});
    command.case_word = read_word(cmd["Word"]);
    for(const auto &item_json : array(cmd["Items"]))
    {
      shell_case_itemt item;
      item.location = location(item_json);
      item.terminator = item_json["Op"].value;
      if(item.terminator != ";;" && item.terminator != ";&")
        unsupported(
          item_json, "case item terminator '" + item.terminator + "'");
      for(const auto &pattern : array(item_json["Patterns"]))
        item.patterns.push_back(read_word(pattern));
      item.body = read_statements(item_json["Stmts"]);
      command.case_items.push_back(std::move(item));
    }
  }

  shell_assignmentt read_assignment(const jsont &assign)
  {
    reject_fields(
      assign,
      {{"Append", "+= assignment (bash/mksh/zsh)"},
       {"Naked", "assignment without '=' (declaration builtin)"},
       {"Index", "array element assignment (bash/mksh/zsh)"},
       {"Array", "array assignment (bash/mksh/zsh)"}});
    shell_assignmentt result;
    result.location = location(assign);
    result.name = assign["Name"]["Value"].value;
    if(assign["Value"].is_object())
      result.value = read_word(assign["Value"]);
    return result;
  }

  shell_redirectt read_redirect(const jsont &redirect, const std::string &op)
  {
    static const std::set<std::string> posix_ops{
      "<", ">", ">|", ">>", "<&", ">&", "<>", "<<", "<<-"};

    shell_redirectt result;
    result.location = location(redirect);
    result.op = op;
    if(posix_ops.count(result.op) == 0)
      unsupported(redirect, "redirection operator '" + result.op + "'");

    const jsont &n = redirect["N"];
    if(n.is_object())
    {
      auto fd = string2optional_size_t(n["Value"].value);
      if(fd.has_value())
        result.fd = static_cast<int>(*fd);
      else
        unsupported(redirect, "named file descriptor {var} (bash/zsh)");
    }

    result.target = read_word(redirect["Word"]);

    const jsont &hdoc = redirect["Hdoc"];
    if(hdoc.is_object())
    {
      if(delimiter_is_quoted(redirect["Word"]))
      {
        // XCU 2.7.4: with any part of the delimiter quoted, the body is
        // not expanded at all.
        shell_word_partt part;
        part.kind = shell_word_partt::kindt::QUOTED_LITERAL;
        part.location = location(hdoc);
        for(const auto &p : array(hdoc["Parts"]))
          part.text += p["Value"].value;
        result.here_document.push_back(std::move(part));
      }
      else
        result.here_document = read_word(hdoc);
    }
    return result;
  }

  static bool delimiter_is_quoted(const jsont &word)
  {
    for(const auto &part : array(word["Parts"]))
    {
      const std::string t = type(part);
      if(t == "SglQuoted" || t == "DblQuoted")
        return true;
      if(t == "Lit" && part["Value"].value.find('\\') != std::string::npos)
        return true;
    }
    return false;
  }

  shell_wordt read_word(const jsont &word)
  {
    shell_wordt result;
    for(const auto &part : array(word["Parts"]))
      result.push_back(read_part(part));
    return result;
  }

  shell_word_partt read_part(const jsont &part)
  {
    using kindt = shell_word_partt::kindt;
    shell_word_partt result;
    result.location = location(part);
    const std::string t = type(part);

    if(t == "Lit")
    {
      result.kind = kindt::LITERAL;
      result.text = part["Value"].value;
    }
    else if(t == "SglQuoted")
    {
      result.kind = kindt::QUOTED_LITERAL;
      result.text = part["Value"].value;
      if(flag(part, "Dollar"))
        result.text =
          decode_dollar_single_quotes(result.text, result.location, log);
    }
    else if(t == "DblQuoted")
    {
      if(flag(part, "Dollar"))
        unsupported(part, "$\"...\" locale translation (bash/mksh)");
      result.kind = kindt::DOUBLE_QUOTED;
      for(const auto &p : array(part["Parts"]))
        result.parts.push_back(read_part(p));
    }
    else if(t == "ParamExp")
      read_parameter(part, result);
    else if(t == "CmdSubst")
    {
      reject_fields(
        part,
        {{"TempFile", "${ ...;} command substitution (mksh)"},
         {"ReplyVar", "${|...;} command substitution (mksh)"}});
      result.kind = kindt::COMMAND_SUBSTITUTION;
      result.commands =
        std::make_shared<shell_command_listt>(read_statements(part["Stmts"]));
    }
    else if(t == "ArithmExp")
    {
      reject_fields(
        part,
        {{"Bracket", "$[...] arithmetic (bash)"},
         {"Unsigned", "$((# ...)) unsigned arithmetic (mksh)"}});
      result.kind = kindt::ARITHMETIC;
      result.text = arithmetic_source(part);
    }
    else if(t == "ExtGlob")
      unsupported(part, "extended glob pattern (bash/mksh/zsh)");
    else if(t == "ProcSubst")
      unsupported(part, "process substitution (bash/zsh)");
    else if(t == "BraceExp")
      unsupported(part, "brace expansion (bash/mksh/zsh)");
    else
      unsupported(part, "word part of type " + t);

    return result;
  }

  void read_parameter(const jsont &part, shell_word_partt &result)
  {
    static const std::set<std::string> posix_ops{
      ":-", "-", ":=", "=", ":?", "?", ":+", "+", "%", "%%", "#", "##"};

    result.kind = shell_word_partt::kindt::PARAMETER;
    reject_fields(
      part,
      {{"Flags", "parameter expansion flags (zsh)"},
       {"Excl", "indirect expansion ${!...} (bash/mksh)"},
       {"Width", "${%...} (mksh)"},
       {"IsSet", "${+...} (zsh)"},
       {"Split", "${=...} (zsh)"},
       {"GlobSubst", "${~...} (zsh)"},
       {"RcExpand", "${^...} (zsh)"},
       {"NestedParam", "nested parameter expansion (zsh)"},
       {"Index", "array subscript (bash/mksh/zsh)"},
       {"Subscripts", "array subscripts (zsh)"},
       {"Modifiers", "parameter modifiers (zsh)"},
       {"Slice", "substring expansion ${var:offset:length} (bash/mksh/zsh)"},
       {"Repl", "pattern substitution ${var/pattern/string} (bash/mksh/zsh)"},
       {"Names", "${!prefix*} name expansion (bash/mksh)"}});
    result.text = part["Param"]["Value"].value;
    result.parameter_length = flag(part, "Length");
    const jsont &exp = part["Exp"];
    if(exp.is_object())
    {
      result.parameter_operator = exp["Op"].value;
      if(posix_ops.count(result.parameter_operator) == 0)
      {
        unsupported(
          part,
          "parameter expansion operator '" + result.parameter_operator + "'");
      }
      if(exp["Word"].is_object())
        result.parts = read_word(exp["Word"]);
    }
  }

  /// The raw text between "$((" and "))", taken from the source.
  std::string arithmetic_source(const jsont &part)
  {
    auto left = string2optional_size_t(part["Left"]["Offset"].value);
    auto right = string2optional_size_t(part["Right"]["Offset"].value);
    if(
      !left.has_value() || !right.has_value() || *left + 3 > *right ||
      *right > source.size() || source.compare(*left, 3, "$((") != 0)
    {
      unsupported(part, "arithmetic expansion with unexpected source offsets");
      return {};
    }
    return source.substr(*left + 3, *right - (*left + 3));
  }
};
} // namespace

bool shfmt_json_to_shell_program(
  const jsont &json,
  const std::string &source,
  const std::string &filename,
  shell_programt &dest,
  message_handlert &message_handler)
{
  if(!json.is_object() || json["Type"].value != "File")
  {
    messaget log{message_handler};
    log.error() << "shfmt output is not a File syntax tree" << messaget::eom;
    return true;
  }
  return shfmt_readert{source, filename, message_handler}.read(json, dest);
}
