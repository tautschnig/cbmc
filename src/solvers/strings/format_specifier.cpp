/*******************************************************************\

Module: Format specifiers for String.format

Author: Romain Brenguier, Joel Allred

Date:   June 2019

\*******************************************************************/

/// \file
///  Format specifiers for String.format

#include "format_specifier.h"

#include <util/config.h>

#include <cctype>
#include <regex>

/// Helper function for parsing format strings.
/// This follows the implementation in openJDK of the java.util.Formatter class:
/// http://hg.openjdk.java.net/jdk7/jdk7/jdk/file/9b8c96f96a0f/src/share/classes/java/util/Formatter.java#l2660
/// \param m: a match in a regular expression
/// \return Format specifier represented by the matched string. The groups in
///   the match should represent: index, flag, width, precision, date and
///   conversion type.
static format_specifiert format_specifier_of_match(std::smatch &m)
{
  PRECONDITION(m.size() == 7);
  // A number beyond the range of int is ignored, an index then relative,
  // and an index of 0 is ordinary, as in Java 8 to 15. Java 16 and later
  // throw for these (see java_format_parse), so that what they print does
  // not matter.
  const auto number = [](const std::string &digits) -> std::optional<int>
  {
    long long value = 0;
    for(const char c : digits)
    {
      if(c < '0' || c > '9')
        break;
      value = value * 10 + (c - '0');
      if(value > 2147483647LL)
        return {};
    }
    return static_cast<int>(value);
  };
  std::string flag = m[2].str();
  int index = -1;
  if(!m[1].str().empty())
  {
    const auto value = number(m[1].str());
    if(!value.has_value())
      flag += '<';
    else if(*value > 0)
      index = *value;
  }
  const auto width_value = number(m[3].str());
  int width =
    m[3].str().empty() || !width_value.has_value() ? -1 : *width_value;
  // The precision is matched with its leading '.'.
  const auto precision_value =
    m[4].str().empty() ? std::optional<int>{} : number(m[4].str().substr(1));
  int precision = precision_value.has_value() ? *precision_value : -1;
  // `tT` values:  "t" = date/time, "T" = uppercase date/time
  std::string tT = m[5].str();

  // date/time: the conversion is the date/time one, and the matched
  // character says which field, which is not modelled.
  bool dt = !tT.empty();

  INVARIANT(
    m[6].str().length() == 1, "format conversion should be one character");
  char conversion = dt ? tT[0] : m[6].str()[0];

  format_specifiert fs(index, flag, width, precision, dt, conversion);
  if(dt)
    fs.date_time_field = m[6].str()[0];
  const std::string known = dt ? "tT" : "bBhHsScCdoxXeEfgGaAtT%n";
  if(known.find(conversion) == std::string::npos)
    fs.unknown_conversion = true;
  return fs;
}

/// Regexp is taken directly from openJDK implementation:
///  http://hg.openjdk.java.net/jdk7/jdk7/jdk/file/9b8c96f96a0f/src/share/classes/java/util/Formatter.java#l2506
std::vector<format_elementt> parse_format_string(std::string s)
{
  const std::string format_specifier =
    "%(\\d+\\$)?([-#+ 0,(\\<]*)?(\\d+)?(\\.\\d+)?([tT])?([a-zA-Z%])";
  std::regex regex(format_specifier);
  std::vector<format_elementt> al;
  std::smatch match;
  // The index of the argument of the last specifier that took one, for the
  // '<' flag, and the number of ordinary specifiers so far.
  int last_index = -1;
  int ordinary_count = 0;

  while(std::regex_search(s, match, regex))
  {
    if(match.position() != 0)
    {
      std::string pre_match = s.substr(0, match.position());
      al.emplace_back(pre_match);
    }

    format_specifiert fs = format_specifier_of_match(match);
    if(
      fs.conversion != format_specifiert::PERCENT_SIGN &&
      fs.conversion != format_specifiert::LINE_SEPARATOR)
    {
      // Java's Formatter: '<' reuses the argument of the previous
      // specifier, an explicit index does not advance the ordinary ones.
      const auto relative = fs.flag.find('<');
      if(relative != std::string::npos)
      {
        fs.flag.erase(relative, 1);
        fs.index = last_index;
        // Without a previous argument, Java throws, which is not modelled.
        if(last_index == -1)
          fs.unsupported = true;
      }
      if(fs.index == -1)
        last_index = ++ordinary_count;
      else
        last_index = fs.index;
      fs.index = last_index;
    }
    al.emplace_back(fs);
    s = match.suffix();
  }

  al.emplace_back(s);
  return al;
}

std::string java_format_flagst::to_string(int flags)
{
  std::string result;
  const std::pair<int, char> order[] = {
    {left_justify, '-'},
    {uppercase, '^'},
    {alternate, '#'},
    {plus, '+'},
    {leading_space, ' '},
    {zero_pad, '0'},
    {group, ','},
    {parentheses, '('},
    {previous, '<'}};
  for(const auto &flag : order)
  {
    if((flags & flag.first) == flag.first)
      result += flag.second;
  }
  return result;
}

std::vector<java_formatter_versiont> java_formatter_versions()
{
  std::vector<java_formatter_versiont> versions;
  if(config.java.may_run_on(8, 15))
    versions.push_back(java_formatter_versiont::JAVA_8);
  if(config.java.may_run_on(16, 18))
    versions.push_back(java_formatter_versiont::JAVA_16);
  if(config.java.may_run_on(19, 0))
    versions.push_back(java_formatter_versiont::JAVA_19);
  // (a runtime before Java 8 runs as Java 8 here)
  if(versions.empty())
    versions.push_back(java_formatter_versiont::JAVA_8);
  return versions;
}

int java_format_bad_flags(
  int flags,
  std::initializer_list<int> bad,
  java_formatter_versiont version)
{
  int result = 0;
  for(const int f : bad)
  {
    if((flags & f) == 0)
      continue;
    if(version != java_formatter_versiont::JAVA_19)
      return f;
    result |= f;
  }
  return result;
}

/// Integer.parseInt of decimal digits, or nullopt beyond the range of int.
static std::optional<int> java_parse_int(const std::string &digits)
{
  long long value = 0;
  for(const char c : digits)
  {
    value = value * 10 + (c - '0');
    if(value > 2147483647LL)
      return std::nullopt;
  }
  return static_cast<int>(value);
}

java_format_parset
java_format_parse(const std::string &s, java_formatter_versiont version)
{
  const bool before_16 = version == java_formatter_versiont::JAVA_8;
  using namespace java_format_flagst;
  namespace error = java_format_errort;
  java_format_parset result;
  const auto fail = [&](char code, const std::string &detail)
  {
    result.error = std::make_pair(code, detail);
    result.specifiers.clear();
    return result;
  };
  const std::string valid = "bBsShHcCdoxXeEgGfaAn%";
  const std::string date_time = "HIklMNLQpsSTzZaAbBCdehjmyYrRcDF";
  const std::regex specifier(
    "%(\\d+\\$)?([-#+ 0,(\\<]*)?(\\d+)?(\\.\\d+)?([tT])?([a-zA-Z%])");
  const std::string int_min = "-2147483648";

  std::size_t i = 0;
  while(i < s.size())
  {
    const std::size_t n = s.find('%', i);
    if(n == std::string::npos)
      break;
    i = n + 1;
    if(i >= s.size())
      return fail(error::unknown_conversion, "%");
    java_format_specifiert fs;
    int width = -1;
    int precision = -1;
    // 0 for an ordinary index, -1 for '<', -2 for no argument
    int index = 0;
    const char first = s[i];
    // A specifier of just a conversion is not checked (FormatSpecifier(char)).
    const bool direct = valid.find(first) != std::string::npos;
    if(direct)
    {
      fs.conversion = first;
      if(std::isupper(static_cast<unsigned char>(first)))
      {
        fs.flags = uppercase;
        fs.conversion = static_cast<char>(std::tolower(first));
      }
      if(first == '%' || first == 'n')
        index = -2;
      ++i;
    }
    else
    {
      std::smatch m;
      const std::string rest = s.substr(n);
      if(!std::regex_search(
           rest, m, specifier, std::regex_constants::match_continuous))
      {
        return fail(error::unknown_conversion, std::string(1, first));
      }
      i = n + m.length(0);
      if(m[1].matched)
      {
        const std::string digits = m[1].str().substr(0, m[1].length() - 1);
        const auto value = java_parse_int(digits);
        if(before_16)
        {
          // NumberFormatException ignored: the index stays -1, relative
          index = value.has_value() ? *value : -1;
        }
        else if(!value.has_value())
          return fail(error::argument_index, int_min);
        else if(*value <= 0)
          return fail(error::argument_index, std::to_string(*value));
        else
          index = *value;
      }
      for(const char c : m[2].str())
      {
        const std::string chars = "-#+ 0,(<";
        const int bits[] = {
          left_justify,
          alternate,
          plus,
          leading_space,
          zero_pad,
          group,
          parentheses,
          previous};
        const int v = bits[chars.find(c)];
        if((fs.flags & v) == v)
          return fail(error::duplicate_flags, to_string(v));
        fs.flags |= v;
      }
      if((fs.flags & previous) != 0)
        index = -1;
      if(m[3].matched)
      {
        const auto value = java_parse_int(m[3].str());
        if(!value.has_value() && !before_16)
          return fail(error::width, int_min);
        width = value.has_value() ? *value : -1;
      }
      if(m[4].matched)
      {
        const auto value = java_parse_int(m[4].str().substr(1));
        if(!value.has_value() && !before_16)
          return fail(error::precision, int_min);
        precision = value.has_value() ? *value : -1;
      }
      if(m[5].matched)
      {
        fs.date_time = true;
        if(m[5].str() == "T")
          fs.flags |= uppercase;
      }
      char c = m[6].str()[0];
      if(!fs.date_time)
      {
        if(valid.find(c) == std::string::npos)
          return fail(error::unknown_conversion, std::string(1, c));
        if(std::isupper(static_cast<unsigned char>(c)))
        {
          fs.flags |= uppercase;
          c = static_cast<char>(std::tolower(c));
        }
        if(c == '%' || c == 'n')
          index = -2;
      }
      fs.conversion = c;
    }

    // Formatter's toString of the specifier
    {
      std::string text = "%" + to_string(fs.flags & ~uppercase);
      if(index > 0)
        text += std::to_string(index) + "$";
      if(width != -1)
        text += std::to_string(width);
      if(precision != -1)
        text += "." + std::to_string(precision);
      const bool upper = (fs.flags & uppercase) != 0;
      if(fs.date_time)
        text += upper ? 'T' : 't';
      text +=
        upper ? static_cast<char>(std::toupper(fs.conversion)) : fs.conversion;
      fs.text = text;
    }

    // the checks of the constructor of FormatSpecifier
    const char c = fs.conversion;
    const int flags = fs.flags;
    const auto contains = [&](int f) { return (flags & f) == f; };
    // the flags among \p bad that the exception names (in the order of the
    // checks of Formatter, which matters before Java 19)
    const auto bad_flags =
      [&](std::initializer_list<int> bad) -> std::optional<std::string>
    {
      const int found = java_format_bad_flags(flags, bad, version);
      if(found != 0)
        return std::string(1, c) + to_string(found);
      return std::nullopt;
    };
    const bool no_width = width == -1;
    const auto check_numeric =
      [&]() -> std::optional<std::pair<char, std::string>>
    {
      if(no_width && (flags & (left_justify | zero_pad)) != 0)
        return std::make_pair(error::missing_width, fs.text);
      if(contains(plus | leading_space) || contains(left_justify | zero_pad))
        return std::make_pair(error::illegal_flags, to_string(flags));
      return std::nullopt;
    };
    if(!direct)
    {
      if(fs.date_time)
      {
        if(precision != -1)
          return fail(error::precision, std::to_string(precision));
        if(date_time.find(c) == std::string::npos)
          return fail(error::unknown_conversion, std::string("t") + c);
        if(
          auto x = bad_flags(
            {alternate, plus, leading_space, zero_pad, group, parentheses}))
          return fail(error::flags_mismatch, *x);
        if(no_width && contains(left_justify))
          return fail(error::missing_width, fs.text);
      }
      else if(c == 'b' || c == 's' || c == 'h')
      {
        if((c == 'b' || c == 'h') && contains(alternate))
          return fail(error::flags_mismatch, std::string(1, c) + "#");
        if(no_width && contains(left_justify))
          return fail(error::missing_width, fs.text);
        if(
          auto x =
            bad_flags({plus, leading_space, zero_pad, group, parentheses}))
          return fail(error::flags_mismatch, *x);
      }
      else if(c == 'c')
      {
        if(precision != -1)
          return fail(error::precision, std::to_string(precision));
        if(
          auto x = bad_flags(
            {alternate, plus, leading_space, zero_pad, group, parentheses}))
          return fail(error::flags_mismatch, *x);
        if(no_width && contains(left_justify))
          return fail(error::missing_width, fs.text);
      }
      else if(c == 'd' || c == 'o' || c == 'x')
      {
        if(auto e = check_numeric())
          return fail(e->first, e->second);
        if(precision != -1)
          return fail(error::precision, std::to_string(precision));
        if(auto x = bad_flags({c == 'd' ? alternate : group}))
          return fail(error::flags_mismatch, *x);
      }
      else if(c == 'e' || c == 'f' || c == 'g' || c == 'a')
      {
        if(auto e = check_numeric())
          return fail(e->first, e->second);
        std::optional<std::string> x;
        if(c == 'a')
          x = bad_flags({parentheses, group});
        else if(c == 'e')
          x = bad_flags({group});
        else if(c == 'g')
          x = bad_flags({alternate});
        if(x.has_value())
          return fail(error::flags_mismatch, *x);
      }
      else
      {
        // %% or %n
        if(precision != -1)
          return fail(error::precision, std::to_string(precision));
        if(c == '%')
        {
          if(flags != left_justify && flags != 0)
            return fail(error::illegal_flags, to_string(flags));
          if(no_width && contains(left_justify))
            return fail(error::missing_width, fs.text);
        }
        else
        {
          if(width != -1)
            return fail(error::width, std::to_string(width));
          if(flags != 0)
            return fail(error::illegal_flags, to_string(flags));
        }
      }
    }
    fs.takes_argument = index != -2;
    fs.argument = index;
    result.specifiers.push_back(fs);
  }

  // The arguments of ordinary and relative indices, as Formatter.format
  // assigns them: an ordinary one advances, a relative one takes the last
  // (0 without one).
  int last = 0;
  int ordinary = 0;
  for(auto &fs : result.specifiers)
  {
    if(!fs.takes_argument)
      continue;
    if(fs.argument == 0)
      fs.argument = last = ++ordinary;
    else if(fs.argument == -1)
      fs.argument = last;
    else
      last = fs.argument;
  }
  return result;
}

int java_format_flags(const std::string &flags, char conversion)
{
  using namespace java_format_flagst;
  const std::string chars = "-#+ 0,(<";
  const int bits[] = {
    left_justify,
    alternate,
    plus,
    leading_space,
    zero_pad,
    group,
    parentheses,
    previous};
  int result = 0;
  for(const char c : flags)
  {
    const std::size_t i = chars.find(c);
    if(i != std::string::npos)
      result |= bits[i];
  }
  if(std::isupper(static_cast<unsigned char>(conversion)))
    result |= uppercase;
  return result;
}

std::vector<java_format_print_versiont> java_format_print_versions()
{
  std::vector<java_format_print_versiont> versions;
  if(config.java.may_run_on(1, 10))
    versions.push_back({false, false});
  if(config.java.may_run_on(9, 16))
    versions.push_back({true, false});
  if(config.java.may_run_on(17, 0))
    versions.push_back({true, true});
  return versions;
}

std::optional<std::string> java_format_decorate(
  char conversion,
  int flags,
  int width,
  int precision,
  const std::string &core,
  bool numeric,
  java_format_print_versiont version)
{
  using namespace java_format_flagst;
  const auto has = [&](int f) { return (flags & f) != 0; };
  std::string body;
  const char c = conversion;
  const bool is_float = c == 'e' || c == 'f' || c == 'g' || c == 'a';
  if(numeric && (c == 'd' || is_float))
  {
    // Formatter.print(long) and print(double): the sign, the magnitude
    // with zeros before it, and the closing parenthesis
    if(has(group))
      return {};
    if(is_float && core == "NaN")
      body = core;
    else
    {
      const bool negative = !core.empty() && core[0] == '-';
      std::string magnitude = core.substr(negative ? 1 : 0);
      // `#` with a precision of 0: the point (for %e before the exponent)
      if(
        has(alternate) && precision == 0 && magnitude != "Infinity" &&
        (c == 'e' || c == 'f'))
      {
        const std::size_t mark =
          c == 'e' ? magnitude.find('e') : magnitude.size();
        magnitude.insert(mark, 1, '.');
      }
      std::string lead;
      if(negative)
        lead = has(parentheses) ? "(" : "-";
      else if(has(plus))
        lead = "+";
      else if(has(leading_space))
        lead = " ";
      const std::string trail = negative && has(parentheses) ? ")" : "";
      std::string zeros;
      if(has(zero_pad) && magnitude != "Infinity")
      {
        if(c == 'a' && precision != -1)
          return {};
        // adjustWidth, and the characters before the magnitude (for %a
        // before Java 17, as if there were no sign)
        const long w = width - (negative && has(parentheses) ? 1 : 0);
        const std::size_t sign =
          c == 'a' && !version.hex_float_counts_sign ? 0 : lead.size();
        const long n = w - static_cast<long>(sign + magnitude.size());
        if(n > 0)
          zeros.assign(static_cast<std::size_t>(n), '0');
      }
      if(c == 'a' && magnitude != "Infinity")
        body = lead + magnitude.substr(0, 2) + zeros + magnitude.substr(2);
      else
        body = lead + zeros + magnitude + trail;
    }
  }
  else if(numeric && (c == 'o' || c == 'x'))
  {
    // Formatter.print(long): the radix prefix, then zeros
    const std::string prefix =
      has(alternate) ? (c == 'o' ? "0" : "0x") : std::string{};
    std::string zeros;
    if(has(zero_pad))
    {
      const long n = width - static_cast<long>(prefix.size() + core.size());
      if(n > 0)
        zeros.assign(static_cast<std::size_t>(n), '0');
    }
    body = prefix + zeros + core;
  }
  else
  {
    // Formatter.print(String): the precision (which only the general
    // conversions take, and the floating-point ones for "null")
    body = core;
    if(precision != -1)
      body = body.substr(0, std::min<std::size_t>(precision, body.size()));
  }
  if(has(uppercase))
  {
    for(char &ch : body)
    {
      if(static_cast<unsigned char>(ch) >= 0x80)
        return {};
      ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
  }
  // appendJustified (Java 8 prints %% without it)
  if(c == '%' && !version.percent_width)
    return body;
  if(width != -1 && static_cast<long>(body.size()) < width)
  {
    const std::string spaces(
      static_cast<std::size_t>(width) - body.size(), ' ');
    body = has(left_justify) ? body + spaces : spaces + body;
  }
  return body;
}
