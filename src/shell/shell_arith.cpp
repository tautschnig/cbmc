/// \file
/// Parser for shell arithmetic expressions (POSIX.1-2024 XCU 2.6.4).
///
/// Recursive descent following the ISO C expression grammar (C11
/// 6.5.1-6.5.17). The comma operator (6.5.17), which XCU 1.1.2.1
/// excludes, is only accepted when the dialect enables it.

#include "shell_arith.h"

#include <util/arith_tools.h>

#include <cctype>

namespace
{
class parse_errort
{
public:
  std::string message;
};

class arith_parsert
{
public:
  arith_parsert(const std::string &text, const shell_arith_optionst &options)
    : text(text),
      mode(options.increment_mode),
      comma(options.comma_operator),
      octal(options.leading_zero_octal)
  {
  }

  shell_arith_exprt parse()
  {
    skip_space();
    if(at_end())
      error("empty arithmetic expression");
    shell_arith_exprt result = parse_expression();
    skip_space();
    if(!at_end())
      error("unexpected '" + text.substr(pos, 1) + "'");
    return result;
  }

  bool contains_increment_sequence = false;

private:
  const std::string &text;
  const shell_arith_increment_modet mode;
  const bool comma;
  const bool octal;
  std::size_t pos = 0;

  using kindt = shell_arith_exprt::kindt;

  [[noreturn]] void error(const std::string &message) const
  {
    throw parse_errort{message + " at offset " + std::to_string(pos)};
  }

  bool at_end() const
  {
    return pos >= text.size();
  }

  void skip_space()
  {
    while(!at_end() && std::isspace(static_cast<unsigned char>(text[pos])))
      ++pos;
  }

  /// Whether \p token starts at the current position (after spaces).
  bool looking_at(const std::string &token)
  {
    skip_space();
    return text.compare(pos, token.size(), token) == 0;
  }

  /// Consume one of \p tokens if present, preferring the longest match,
  /// unless it is directly followed by a character in \p not_followed_by.
  std::optional<std::string> accept(
    std::initializer_list<const char *> tokens,
    const std::string &not_followed_by = "")
  {
    skip_space();
    std::string best;
    for(const char *token : tokens)
    {
      std::string t{token};
      if(t.size() > best.size() && text.compare(pos, t.size(), t) == 0)
        best = t;
    }
    if(best.empty())
      return {};
    const std::size_t next = pos + best.size();
    if(
      next < text.size() &&
      not_followed_by.find(text[next]) != std::string::npos)
    {
      return {};
    }
    pos = next;
    return best;
  }

  void expect(char c)
  {
    skip_space();
    if(at_end() || text[pos] != c)
      error(std::string{"expected '"} + c + "'");
    ++pos;
  }

  static shell_arith_exprt
  binary(const std::string &op, shell_arith_exprt lhs, shell_arith_exprt rhs)
  {
    shell_arith_exprt result;
    result.kind = kindt::BINARY;
    result.op = op;
    result.offset = lhs.offset;
    result.operands.push_back(std::move(lhs));
    result.operands.push_back(std::move(rhs));
    return result;
  }

  // expression: assignment { , assignment }   (comma only if enabled)
  shell_arith_exprt parse_expression()
  {
    shell_arith_exprt lhs = parse_assignment();
    while(comma && accept({","}).has_value())
      lhs = binary(",", std::move(lhs), parse_assignment());
    return lhs;
  }

  // assignment-expression: conditional | VARIABLE assign-op assignment
  shell_arith_exprt parse_assignment()
  {
    shell_arith_exprt lhs = parse_conditional();
    auto op = accept(
      {"=", "*=", "/=", "%=", "+=", "-=", "<<=", ">>=", "&=", "^=", "|="}, "=");
    if(!op.has_value())
      return lhs;
    if(lhs.kind != kindt::VARIABLE)
      error("assignment '" + *op + "' requires a variable");
    shell_arith_exprt result;
    result.kind = kindt::ASSIGNMENT;
    result.op = *op;
    result.name = lhs.name;
    result.offset = lhs.offset;
    result.operands.push_back(parse_assignment());
    return result;
  }

  // conditional-expression: logical-or [ ? expression : conditional ]
  shell_arith_exprt parse_conditional()
  {
    shell_arith_exprt condition = parse_logical_or();
    if(!accept({"?"}).has_value())
      return condition;
    shell_arith_exprt then_case = parse_expression();
    expect(':');
    // C makes the third operand a conditional-expression, but shells
    // accept an assignment there ("c ? x = 1 : x = 2"); follow them.
    shell_arith_exprt else_case = parse_assignment();
    shell_arith_exprt result;
    result.kind = kindt::CONDITIONAL;
    result.offset = condition.offset;
    result.operands.push_back(std::move(condition));
    result.operands.push_back(std::move(then_case));
    result.operands.push_back(std::move(else_case));
    return result;
  }

  shell_arith_exprt parse_logical_or()
  {
    shell_arith_exprt lhs = parse_logical_and();
    while(accept({"||"}).has_value())
      lhs = binary("||", std::move(lhs), parse_logical_and());
    return lhs;
  }

  shell_arith_exprt parse_logical_and()
  {
    shell_arith_exprt lhs = parse_bitwise_or();
    while(accept({"&&"}).has_value())
      lhs = binary("&&", std::move(lhs), parse_bitwise_or());
    return lhs;
  }

  shell_arith_exprt parse_bitwise_or()
  {
    shell_arith_exprt lhs = parse_bitwise_xor();
    while(accept({"|"}, "|=").has_value())
      lhs = binary("|", std::move(lhs), parse_bitwise_xor());
    return lhs;
  }

  shell_arith_exprt parse_bitwise_xor()
  {
    shell_arith_exprt lhs = parse_bitwise_and();
    while(accept({"^"}, "=").has_value())
      lhs = binary("^", std::move(lhs), parse_bitwise_and());
    return lhs;
  }

  shell_arith_exprt parse_bitwise_and()
  {
    shell_arith_exprt lhs = parse_equality();
    while(accept({"&"}, "&=").has_value())
      lhs = binary("&", std::move(lhs), parse_equality());
    return lhs;
  }

  shell_arith_exprt parse_equality()
  {
    shell_arith_exprt lhs = parse_relational();
    while(auto op = accept({"==", "!="}))
      lhs = binary(*op, std::move(lhs), parse_relational());
    return lhs;
  }

  shell_arith_exprt parse_relational()
  {
    shell_arith_exprt lhs = parse_shift();
    // "<" and ">" must not start "<<" / ">>" or "<<=" / ">>=".
    while(auto op = accept({"<=", ">=", "<", ">"}, "<>"))
      lhs = binary(*op, std::move(lhs), parse_shift());
    return lhs;
  }

  shell_arith_exprt parse_shift()
  {
    shell_arith_exprt lhs = parse_additive();
    while(auto op = accept({"<<", ">>"}, "="))
      lhs = binary(*op, std::move(lhs), parse_additive());
    return lhs;
  }

  shell_arith_exprt parse_additive()
  {
    shell_arith_exprt lhs = parse_multiplicative();
    while(true)
    {
      skip_space();
      if(at_end() || (text[pos] != '+' && text[pos] != '-'))
        break;
      const char c = text[pos];
      const bool doubled = pos + 1 < text.size() && text[pos + 1] == c;
      if(pos + 1 < text.size() && text[pos + 1] == '=')
        break; // += or -=
      if(doubled)
      {
        // "a--b": a decrement operator in a binary position. With strict
        // increment operators this is a syntax error; dash and bash
        // read it as "a - -b".
        if(mode == shell_arith_increment_modet::STRICT)
          error(std::string{c} + c + " requires a variable");
        if(mode == shell_arith_increment_modet::NONE)
          contains_increment_sequence = true;
      }
      ++pos;
      lhs = binary(std::string{c}, std::move(lhs), parse_multiplicative());
    }
    return lhs;
  }

  shell_arith_exprt parse_multiplicative()
  {
    shell_arith_exprt lhs = parse_unary();
    while(auto op = accept({"*", "/", "%"}, "="))
      lhs = binary(*op, std::move(lhs), parse_unary());
    return lhs;
  }

  shell_arith_exprt parse_unary()
  {
    skip_space();
    const std::size_t start = pos;

    if(looking_at("++") || looking_at("--"))
    {
      const std::string op = text.substr(pos, 2);
      if(mode == shell_arith_increment_modet::NONE)
      {
        // dash: two sign operators.
        contains_increment_sequence = true;
      }
      else
      {
        const std::size_t saved = pos;
        pos += 2;
        shell_arith_exprt operand = parse_unary();
        if(operand.kind == kindt::VARIABLE)
        {
          shell_arith_exprt result;
          result.kind = kindt::UNARY;
          result.op = op;
          result.offset = start;
          result.operands.push_back(std::move(operand));
          return result;
        }
        if(mode == shell_arith_increment_modet::STRICT)
          error(op + " requires a variable");
        // LENIENT: re-parse as two sign operators.
        pos = saved;
      }
    }

    if(auto op = accept({"+", "-", "~", "!"}, "="))
    {
      shell_arith_exprt result;
      result.kind = kindt::UNARY;
      result.op = *op;
      result.offset = start;
      result.operands.push_back(parse_unary());
      return result;
    }

    return parse_postfix();
  }

  shell_arith_exprt parse_postfix()
  {
    shell_arith_exprt operand = parse_primary();
    if(
      mode != shell_arith_increment_modet::NONE &&
      operand.kind == kindt::VARIABLE && (looking_at("++") || looking_at("--")))
    {
      shell_arith_exprt result;
      result.kind = kindt::POSTFIX;
      result.op = text.substr(pos, 2);
      result.offset = operand.offset;
      pos += 2;
      result.operands.push_back(std::move(operand));
      return result;
    }
    return operand;
  }

  shell_arith_exprt parse_primary()
  {
    skip_space();
    if(at_end())
      error("expecting an operand");

    shell_arith_exprt result;
    result.offset = pos;
    const char c = text[pos];

    if(c == '(')
    {
      ++pos;
      result = parse_expression();
      expect(')');
      return result;
    }

    if(
      c == '0' && pos + 2 < text.size() &&
      (text[pos + 1] == 'x' || text[pos + 1] == 'X') && text[pos + 2] == '$')
    {
      // 0x$x: the digits of a hexadecimal constant are the value of x.
      pos += 3;
      result.kind = kindt::PARAMETER;
      result.prefix = "0x";
      result.name = parse_parameter(result);
      if(!at_end() && (is_name_char(text[pos]) || text[pos] == '$'))
        error("expansion that is part of a token in arithmetic expression");
      return result;
    }

    if(std::isdigit(static_cast<unsigned char>(c)))
    {
      result.kind = kindt::CONSTANT;
      result.value = parse_constant();
      return result;
    }

    if(is_name_start(c))
    {
      result.kind = kindt::VARIABLE;
      result.name = parse_name();
      return result;
    }

    if(c == '$')
    {
      ++pos;
      result.kind = kindt::PARAMETER;
      result.name = parse_parameter(result);
      return result;
    }

    error("expecting an operand");
  }

  static bool is_name_start(char c)
  {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
  }

  static bool is_name_char(char c)
  {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
  }

  std::string parse_name()
  {
    const std::size_t start = pos;
    while(!at_end() && is_name_char(text[pos]))
      ++pos;
    return text.substr(start, pos - start);
  }

  /// After "$": a name, a single digit, a special parameter, or a braced
  /// name or number. Other ${...} forms must be expanded by the caller.
  std::string parse_parameter(shell_arith_exprt &result)
  {
    if(at_end())
      error("expecting a parameter after '$'");
    const char c = text[pos];
    if(c == '{')
    {
      ++pos;
      std::string name;
      if(!at_end() && std::isdigit(static_cast<unsigned char>(text[pos])))
      {
        while(!at_end() && std::isdigit(static_cast<unsigned char>(text[pos])))
          name += text[pos++];
      }
      else if(!at_end() && is_name_start(text[pos]))
        name = parse_name();
      else if(
        !at_end() &&
        std::string{"@*#?-$!"}.find(text[pos]) != std::string::npos)
        name = text.substr(pos++, 1);
      if(name.empty() || at_end())
        error("unsupported parameter expansion in arithmetic expression");
      if(text[pos] != '}')
        parse_parameter_operator(result);
      ++pos;
      return name;
    }
    if(is_name_start(c))
      return parse_name();
    if(
      std::isdigit(static_cast<unsigned char>(c)) ||
      std::string{"@*#?-$!"}.find(c) != std::string::npos)
    {
      ++pos;
      return std::string{c};
    }
    error("expecting a parameter after '$'");
  }

  /// The operator and word of ${name<op>word}, up to the closing '}'. Only
  /// words of literal text are supported: other expansions and quoting in
  /// the word would have to be expanded before the expression is parsed.
  void parse_parameter_operator(shell_arith_exprt &result)
  {
    std::string op;
    if(text[pos] == ':')
      op += text[pos++];
    if(
      at_end() || std::string{"-=?+%#"}.find(text[pos]) == std::string::npos ||
      (!op.empty() && (text[pos] == '%' || text[pos] == '#')))
    {
      error("unsupported parameter expansion in arithmetic expression");
    }
    op += text[pos++];
    // ${x%%p} and ${x##p}
    if((op == "%" || op == "#") && !at_end() && text[pos] == op[0])
    {
      op += text[pos++];
    }
    const std::size_t end = text.find('}', pos);
    if(end == std::string::npos)
      error("unterminated parameter expansion in arithmetic expression");
    const std::string word = text.substr(pos, end - pos);
    if(word.find_first_of("$`\\'\"{") != std::string::npos)
    {
      error(
        "parameter expansion with a word that is not literal text in "
        "arithmetic expression");
    }
    result.op = op;
    result.word = word;
    pos = end;
  }

  /// A decimal, octal (leading 0), or hexadecimal (0x) constant, per ISO
  /// C 6.4.4.1, without integer suffixes.
  mp_integer parse_constant()
  {
    unsigned base = 10;
    std::size_t start = pos;
    if(
      text[pos] == '0' && pos + 1 < text.size() &&
      (text[pos + 1] == 'x' || text[pos + 1] == 'X'))
    {
      base = 16;
      pos += 2;
      start = pos;
    }
    else if(text[pos] == '0' && octal)
      base = 8;

    while(!at_end() && is_name_char(text[pos]))
      ++pos;
    const std::string digits = text.substr(start, pos - start);

    if(digits.empty())
      error("missing hexadecimal digits");
    mp_integer value = 0;
    for(char d : digits)
    {
      unsigned digit;
      if(std::isdigit(static_cast<unsigned char>(d)))
        digit = static_cast<unsigned>(d - '0');
      else if(base == 16 && std::isxdigit(static_cast<unsigned char>(d)))
        digit = static_cast<unsigned>(
          std::tolower(static_cast<unsigned char>(d)) - 'a' + 10);
      else
        error("invalid character '" + std::string{d} + "' in constant");
      if(digit >= base)
        error("invalid digit '" + std::string{d} + "' in constant");
      value = value * base + digit;
    }
    return value;
  }
};
} // namespace

shell_arith_parse_resultt parse_shell_arithmetic(
  const std::string &text,
  const shell_arith_optionst &options)
{
  shell_arith_parse_resultt result;
  arith_parsert parser{text, options};
  try
  {
    result.expression = parser.parse();
  }
  catch(const parse_errort &e)
  {
    result.error = e.message;
  }
  result.contains_increment_sequence = parser.contains_increment_sequence;
  return result;
}

std::string to_string(const shell_arith_exprt &expr)
{
  using kindt = shell_arith_exprt::kindt;
  switch(expr.kind)
  {
  case kindt::CONSTANT:
    return integer2string(expr.value);
  case kindt::VARIABLE:
    return expr.name;
  case kindt::PARAMETER:
    if(!expr.op.empty())
      return "${" + expr.name + expr.op + expr.word + "}";
    return "$" + expr.name;
  case kindt::UNARY:
    return "(" + expr.op + to_string(expr.operands[0]) + ")";
  case kindt::POSTFIX:
    return "(" + to_string(expr.operands[0]) + expr.op + ")";
  case kindt::BINARY:
    return "(" + to_string(expr.operands[0]) + " " + expr.op + " " +
           to_string(expr.operands[1]) + ")";
  case kindt::CONDITIONAL:
    return "(" + to_string(expr.operands[0]) + " ? " +
           to_string(expr.operands[1]) + " : " + to_string(expr.operands[2]) +
           ")";
  case kindt::ASSIGNMENT:
    return "(" + expr.name + " " + expr.op + " " + to_string(expr.operands[0]) +
           ")";
  }
  return "?";
}
