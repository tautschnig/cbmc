/// \file
/// Shell patterns (POSIX.1-2024 XCU 2.14).

#include "shell_pattern.h"

#include <util/invariant.h>

#include "shell_runtime.h"

#include <cctype>
#include <sstream>

namespace
{
using elementt = shell_pattern_elementt;
using kindt = shell_pattern_elementt::kindt;

class pattern_compilert
{
public:
  pattern_compilert(shell_dialectt dialect, std::string &error)
    : dialect(dialect), error(error)
  {
  }

  bool compile(const shell_wordt &word, shell_patternt &pattern)
  {
    using part_kindt = shell_word_partt::kindt;
    for(const auto &part : word)
    {
      switch(part.kind)
      {
      case part_kindt::LITERAL:
        if(!unquoted(part.text, pattern))
          return false;
        break;
      case part_kindt::QUOTED_LITERAL:
        quoted(part.text, pattern);
        break;
      case part_kindt::DOUBLE_QUOTED:
        for(const auto &inner : part.parts)
        {
          if(inner.kind == part_kindt::LITERAL)
            quoted(double_quoted_text(inner.text), pattern);
          else if(inner.kind == part_kindt::QUOTED_LITERAL)
            quoted(inner.text, pattern);
          else
            dynamic(inner, pattern);
        }
        break;
      case part_kindt::PARAMETER:
      case part_kindt::COMMAND_SUBSTITUTION:
      case part_kindt::ARITHMETIC:
        // Unquoted: the value is itself a pattern.
        if(part.kind == part_kindt::ARITHMETIC)
          dynamic(part, pattern); // a number has no pattern characters
        else
        {
          error =
            "unquoted expansion in a pattern (its value would be a "
            "pattern); quote it";
          return false;
        }
        break;
      }
    }
    return true;
  }

private:
  const shell_dialectt dialect;
  std::string &error;

  static void character(unsigned char c, shell_patternt &pattern)
  {
    elementt element;
    element.kind = kindt::CHARACTER;
    element.character = c;
    pattern.elements.push_back(element);
  }

  static void quoted(const std::string &text, shell_patternt &pattern)
  {
    for(char c : text)
      character(static_cast<unsigned char>(c), pattern);
  }

  static void dynamic(const shell_word_partt &part, shell_patternt &pattern)
  {
    elementt element;
    element.kind = kindt::DYNAMIC;
    element.dynamic_index = pattern.dynamic_parts.size();
    pattern.dynamic_parts.push_back(&part);
    pattern.elements.push_back(element);
  }

  /// Inside double quotes, a backslash only escapes $ ` " \ and newline.
  static std::string double_quoted_text(const std::string &text)
  {
    std::string result;
    for(std::size_t i = 0; i < text.size(); ++i)
    {
      if(text[i] == '\\' && i + 1 < text.size())
      {
        const char next = text[i + 1];
        if(next == '$' || next == '`' || next == '"' || next == '\\')
        {
          result += next;
          ++i;
          continue;
        }
        if(next == '\n')
        {
          ++i;
          continue;
        }
      }
      result += text[i];
    }
    return result;
  }

  bool unquoted(const std::string &text, shell_patternt &pattern)
  {
    for(std::size_t i = 0; i < text.size(); ++i)
    {
      const char c = text[i];
      if(c == '\\')
      {
        if(i + 1 < text.size())
          character(static_cast<unsigned char>(text[++i]), pattern);
        continue;
      }
      if(c == '*')
      {
        // ** is the same as *.
        if(
          pattern.elements.empty() ||
          pattern.elements.back().kind != kindt::STAR)
        {
          elementt element;
          element.kind = kindt::STAR;
          pattern.elements.push_back(element);
        }
        continue;
      }
      if(c == '?')
      {
        elementt element;
        element.kind = kindt::ANY;
        pattern.elements.push_back(element);
        continue;
      }
      if(c == '[')
      {
        std::size_t end;
        elementt element;
        const auto parsed = bracket(text, i, element, end);
        if(!parsed.has_value())
          return false;
        if(*parsed)
        {
          pattern.elements.push_back(element);
          i = end;
          continue;
        }
        // Without a closing ], [ is an ordinary character (XCU 2.14.1).
      }
      if(dialect == shell_dialectt::BASH && (c == '(' || c == '|'))
      {
        // Possibly an extended glob; shfmt reports those already.
      }
      character(static_cast<unsigned char>(c), pattern);
    }
    return true;
  }

  /// Parse a bracket expression starting at text[start] == '['.
  /// \return true and set \p end to the closing ], false if there is no
  ///   closing ] (so that [ is literal), or nothing on error
  std::optional<bool> bracket(
    const std::string &text,
    std::size_t start,
    elementt &element,
    std::size_t &end)
  {
    std::size_t i = start + 1;
    bool negate = false;
    if(i < text.size() && text[i] == '!')
    {
      negate = true;
      ++i;
    }
    else if(i < text.size() && text[i] == '^')
    {
      // POSIX leaves [^...] unspecified: dash and mksh read ^ literally,
      // bash, zsh, busybox, and yash as negation.
      if(dialect == shell_dialectt::POSIX || dialect == shell_dialectt::MKSH)
      {
        error = "[^...] in a pattern, whose meaning is unspecified; use [!...]";
        return {};
      }
      negate = true;
      ++i;
    }
    std::bitset<256> set;
    bool first = true;
    while(i < text.size())
    {
      unsigned char c = static_cast<unsigned char>(text[i]);
      if(c == ']' && !first)
      {
        element.kind = kindt::SET;
        element.set = negate ? ~set : set;
        end = i;
        return true;
      }
      first = false;
      if(c == '[' && i + 1 < text.size() && text[i + 1] == ':')
      {
        const std::size_t close = text.find(":]", i + 2);
        if(close == std::string::npos)
          return false;
        const std::string name = text.substr(i + 2, close - i - 2);
        if(!add_class(name, set))
        {
          error = "character class [:" + name + ":] in a pattern";
          return {};
        }
        i = close + 2;
        continue;
      }
      if(
        c == '[' && i + 1 < text.size() &&
        (text[i + 1] == '=' || text[i + 1] == '.'))
      {
        error = "equivalence class or collating symbol in a pattern";
        return {};
      }
      if(c == '\\' && i + 1 < text.size())
        c = static_cast<unsigned char>(text[++i]);
      // A range c-d, unless - is last.
      if(i + 2 < text.size() && text[i + 1] == '-' && text[i + 2] != ']')
      {
        unsigned char last = static_cast<unsigned char>(text[i + 2]);
        std::size_t next = i + 3;
        if(last == '\\' && i + 3 < text.size())
        {
          last = static_cast<unsigned char>(text[i + 3]);
          next = i + 4;
        }
        if(last == '[')
        {
          error = "range ending in a class in a pattern";
          return {};
        }
        // In the C locale, ranges are by byte value.
        for(unsigned v = c; v <= last; ++v)
          set.set(v);
        i = next;
        continue;
      }
      set.set(c);
      ++i;
    }
    return false;
  }

  static bool add_class(const std::string &name, std::bitset<256> &set)
  {
    int (*predicate)(int) = nullptr;
    if(name == "alnum")
      predicate = isalnum;
    else if(name == "alpha")
      predicate = isalpha;
    else if(name == "blank")
      predicate = isblank;
    else if(name == "cntrl")
      predicate = iscntrl;
    else if(name == "digit")
      predicate = isdigit;
    else if(name == "graph")
      predicate = isgraph;
    else if(name == "lower")
      predicate = islower;
    else if(name == "print")
      predicate = isprint;
    else if(name == "punct")
      predicate = ispunct;
    else if(name == "space")
      predicate = isspace;
    else if(name == "upper")
      predicate = isupper;
    else if(name == "xdigit")
      predicate = isxdigit;
    else
      return false;
    // The C locale: the ASCII classes.
    for(unsigned v = 0; v < 128; ++v)
    {
      if(predicate(static_cast<int>(v)))
        set.set(v);
    }
    return true;
  }
};
} // namespace

std::optional<shell_patternt> compile_shell_pattern(
  const shell_wordt &word,
  shell_dialectt dialect,
  std::string &error)
{
  shell_patternt pattern;
  if(!pattern_compilert{dialect, error}.compile(word, pattern))
    return {};
  return pattern;
}

std::string shell_patternt::signature() const
{
  std::ostringstream out;
  for(const auto &element : elements)
  {
    switch(element.kind)
    {
    case kindt::CHARACTER:
      out << 'c' << static_cast<unsigned>(element.character) << ';';
      break;
    case kindt::ANY:
      out << '?';
      break;
    case kindt::SET:
      out << '[' << element.set.to_string() << ']';
      break;
    case kindt::DYNAMIC:
      out << 'd';
      break;
    case kindt::STAR:
      out << '*';
      break;
    }
  }
  return out.str();
}

bool shell_patternt::is_literal() const
{
  for(const auto &element : elements)
  {
    if(element.kind != kindt::CHARACTER && element.kind != kindt::DYNAMIC)
    {
      return false;
    }
  }
  return true;
}

shell_pattern_functionst shell_pattern_function_names(const std::string &p)
{
  return {
    p + "_match",
    p + "_remove_shortest_prefix",
    p + "_remove_longest_prefix",
    p + "_remove_shortest_suffix",
    p + "_remove_longest_suffix"};
}

namespace
{
/// The C condition that byte expression \p c matches \p element.
std::string element_condition(const elementt &element, const std::string &c)
{
  if(element.kind == kindt::CHARACTER)
    return c + " == " + std::to_string(element.character);
  if(element.kind == kindt::ANY)
    return "1";
  // A set, as ranges of byte values.
  std::string result;
  for(unsigned v = 0; v < 256; ++v)
  {
    if(!element.set.test(v))
      continue;
    unsigned last = v;
    while(last + 1 < 256 && element.set.test(last + 1))
      ++last;
    if(!result.empty())
      result += " || ";
    if(last == v)
      result += c + " == " + std::to_string(v);
    else
    {
      result += "(" + c + " >= " + std::to_string(v) + " && " + c +
                " <= " + std::to_string(last) + ")";
    }
    v = last;
  }
  return result.empty() ? "0" : "(" + result + ")";
}

class matcher_writert
{
public:
  matcher_writert(
    const shell_patternt &pattern,
    const std::string &prefix,
    std::size_t max_length)
    : elements(pattern.elements),
      names(shell_pattern_function_names(prefix)),
      length(max_length)
  {
    PRECONDITION(pattern.dynamic_parts.empty());
  }

  std::string str()
  {
    write_match();
    write_prefix_removal(true);
    write_prefix_removal(false);
    write_suffix_removal(true);
    write_suffix_removal(false);
    return out.str();
  }

private:
  const std::vector<elementt> &elements;
  const shell_pattern_functionst names;
  const std::size_t length;
  std::ostringstream out;

  std::string b(std::size_t i, std::size_t j)
  {
    return "b" + std::to_string(i) + "_" + std::to_string(j);
  }

  std::string f(std::size_t i, std::size_t j)
  {
    return "f" + std::to_string(i) + "_" + std::to_string(j);
  }

  std::string byte(std::size_t i)
  {
    return "s.data[" + std::to_string(i) + "]";
  }

  /// b_i_j: s[i..n) matches elements[j..].
  void backward_table()
  {
    const std::size_t p = elements.size();
    for(std::size_t i = length + 1; i-- > 0;)
    {
      out << "  _Bool " << b(i, p) << " = s.length == " << i << "ull;\n";
      for(std::size_t j = p; j-- > 0;)
      {
        const elementt &element = elements[j];
        out << "  _Bool " << b(i, j) << " = ";
        if(element.kind == kindt::STAR)
        {
          out << b(i, j + 1);
          if(i < length)
            out << " || (" << i << "ull < s.length && " << b(i + 1, j) << ")";
        }
        else if(i < length)
        {
          out << "(" << i << "ull < s.length && "
              << element_condition(element, byte(i)) << " && "
              << b(i + 1, j + 1) << ")";
        }
        else
          out << "0";
        out << ";\n";
      }
    }
  }

  /// f_i_j: s[0..i) matches elements[..j).
  void forward_table()
  {
    const std::size_t p = elements.size();
    for(std::size_t i = 0; i <= length; ++i)
    {
      for(std::size_t j = 0; j <= p; ++j)
      {
        out << "  _Bool " << f(i, j) << " = ";
        if(j == 0)
          out << (i == 0 ? "1" : "0");
        else
        {
          const elementt &element = elements[j - 1];
          if(element.kind == kindt::STAR)
          {
            out << f(i, j - 1);
            if(i > 0)
              out << " || " << f(i - 1, j);
          }
          else if(i > 0)
          {
            out << "(" << f(i - 1, j - 1) << " && "
                << element_condition(element, byte(i - 1)) << ")";
          }
          else
            out << "0";
        }
        out << ";\n";
      }
    }
  }

  void begin(const std::string &return_type, const std::string &name)
  {
    shell_runtime_function_begin(
      out, return_type + " " + name + "(struct " SHELL_STRING_TAG " s)");
  }

  void write_match()
  {
    begin("_Bool", names.match);
    backward_table();
    out << "  return " << b(0, 0) << ";\n";
    shell_runtime_function_end(out);
  }

  /// ${s#pattern} and ${s##pattern}: remove the shortest or longest
  /// matching prefix s[0..k).
  void write_prefix_removal(bool shortest)
  {
    begin(
      "struct " SHELL_STRING_TAG,
      shortest ? names.remove_shortest_prefix : names.remove_longest_prefix);
    forward_table();
    out << "  long long k = -1;\n";
    const std::size_t p = elements.size();
    // For the shortest prefix, the last assignment must be for the
    // smallest k.
    for(std::size_t step = 0; step <= length; ++step)
    {
      const std::size_t i = shortest ? length - step : step;
      out << "  if(" << i << "ull <= s.length && " << f(i, p) << ")\n"
          << "    k = " << i << ";\n";
    }
    out << "  if(k < 0)\n"
        << "    return s;\n"
        << "  return " SHELL_RT_SUBSTRING
           "(s, (unsigned long long)k, "
           "s.length - (unsigned long long)k);\n";
    shell_runtime_function_end(out);
  }

  /// ${s%pattern} and ${s%%pattern}: remove the shortest or longest
  /// matching suffix s[k..n).
  void write_suffix_removal(bool shortest)
  {
    begin(
      "struct " SHELL_STRING_TAG,
      shortest ? names.remove_shortest_suffix : names.remove_longest_suffix);
    backward_table();
    out << "  long long k = -1;\n";
    // The shortest suffix has the largest k.
    for(std::size_t step = 0; step <= length; ++step)
    {
      const std::size_t i = shortest ? step : length - step;
      out << "  if(" << i << "ull <= s.length && " << b(i, 0) << ")\n"
          << "    k = " << i << ";\n";
    }
    out << "  if(k < 0)\n"
        << "    return s;\n"
        << "  return " SHELL_RT_SUBSTRING "(s, 0, (unsigned long long)k);\n";
    shell_runtime_function_end(out);
  }
};
} // namespace

std::string shell_pattern_matcher_source(
  const shell_patternt &pattern,
  const std::string &prefix,
  std::size_t max_length)
{
  return shell_runtime_declarations(max_length) +
         matcher_writert{pattern, prefix, max_length}.str();
}
