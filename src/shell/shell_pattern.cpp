/// \file
/// Shell patterns (POSIX.1-2024 XCU 2.14).

#include "shell_pattern.h"

#include <util/invariant.h>

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
