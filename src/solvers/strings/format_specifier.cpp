/*******************************************************************\

Module: Format specifiers for String.format

Author: Romain Brenguier, Joel Allred

Date:   June 2019

\*******************************************************************/

/// \file
///  Format specifiers for String.format

#include "format_specifier.h"
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
  int index = m[1].str().empty() ? -1 : std::stoi(m[1].str());
  std::string flag = m[2].str();
  int width = m[3].str().empty() ? -1 : std::stoi(m[3].str());
  // The precision is matched with its leading '.'.
  int precision = m[4].str().empty() ? -1 : std::stoi(m[4].str().substr(1));
  // `tT` values:  "t" = date/time, "T" = uppercase date/time
  std::string tT = m[5].str();

  // date/time: the conversion is the date/time one, and the matched
  // character says which field, which is not modelled.
  bool dt = !tT.empty();

  INVARIANT(
    m[6].str().length() == 1, "format conversion should be one character");
  char conversion = dt ? tT[0] : m[6].str()[0];

  return format_specifiert(index, flag, width, precision, dt, conversion);
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
