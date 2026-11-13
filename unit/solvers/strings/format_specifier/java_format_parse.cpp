/*******************************************************************\

Module: Unit tests for java_format_parse

Author: Diffblue Ltd.

\*******************************************************************/

#include <solvers/strings/format_specifier.h>
#include <testing-utils/use_catch.h>

#include <string>
#include <utility>
#include <vector>

/// The result of java_format_parse as "E code detail", or as "OK" and, for
/// each specifier, its text and its argument (-2 for none).
static std::string show(const java_format_parset &result)
{
  if(result.error.has_value())
    return std::string{"E "} + result.error->first + " " + result.error->second;
  std::string text = "OK";
  for(const auto &fs : result.specifiers)
  {
    text += " [" + fs.text + "|" +
            std::to_string(fs.takes_argument ? fs.argument : -2) + "]";
  }
  return text;
}

// The expected results are those of java.util.Formatter of Java 21 (as
// those of Java 17 and 25), with the arguments that Formatter.format takes
// for relative and ordinary indices.
TEST_CASE(
  "java_format_parse as java.util.Formatter",
  "[core][solvers][strings][format_specifier]")
{
  const std::vector<std::pair<std::string, std::string>> cases{
    {"%1$--5.0a", "E D -"},
    {"x y%5b%((.0C%#G", "E D ("},
    {"%001S%H", "E D 0"},
    {"%++Sa%(5te", "E D +"},
    {"%3$5d%++.2E", "E D +"},
    {"%H%  Tz", "E D  "},
    {"%-n", "E F -"},
    {"%te%%%2$0%%n%00.0e", "E F 0"},
    {"%n%,%a%B%aa%2$#5h", "E F ,"},
    {"%0%%n%5.0ga%%", "E F 0"},
    {"%1$12tYx y% 5%%%%<012te", "E F  "},
    {"%(<12%%1.0%%1g", "E F (<"},
    {"%-(o%n%<+B%n", "E M %-(o"},
    {"%TH%-<g", "E M %-<g"},
    {"%2$5.2H%-aa%Tz%%%12s", "E M %-a"},
    {"%d%12c% 12d%0(e", "E M %0(e"},
    {"%2$12b%,-xx y%12Sa%1tL", "E M %-,x"},
    {"%tL%x%%%1X%3$#-s", "E M %-#3$s"},
    {"%Ea%d%n", "OK [%E|1] [%d|2] [%n|-2]"},
    {"%12c%%%12te%tY%%%#f",
     "OK [%12c|1] [%%|-2] [%12te|2] [%tY|3] [%%|-2] [%#f|4]"},
    {"%1.0b%1$- 5.2f", "OK [%1.0b|1] [%- 1$5.2f|1]"},
    {"%n%3$5tY", "OK [%n|-2] [%3$5tY|3]"},
    {"%.0Ax y%1.0E", "OK [%.0A|1] [%1.0E|2]"},
    {"%2$b", "OK [%2$b|2]"},
    {"%5G%S", "OK [%5G|1] [%S|2]"},
    {"%G", "OK [%G|1]"},
    {"%2$5x", "OK [%2$5x|2]"},
    {"%e%3$S", "OK [%e|1] [%3$S|3]"},
    {"%3$B%12X%tY", "OK [%3$B|3] [%12X|1] [%tY|2]"},
    {"a%+.0A%h%G", "OK [%+.0A|1] [%h|2] [%G|3]"},
    {"x y%E%12x%n%C", "OK [%E|1] [%12x|2] [%n|-2] [%C|3]"},
    {"%X", "OK [%X|1]"},
    {"%1.0X%%%# 5n%12c", "E P 0"},
    {"x y%3$.0tY%#(s", "E P 0"},
    {"%n% fx y%.2b% 1A%5.0n", "E P 0"},
    {"%.2Tz%5TH", "E P 2"},
    {"%.0tQ%#<12S%Tz%S", "E P 0"},
    {"%%%f%.0x", "E P 0"},
    {"%1$B%E%%%12n", "E W 12"},
    {"x y%2$5n%1B%s", "E W 5"},
    {"%tY%5na%1$5H", "E W 5"},
    {"%-01n%%%1o", "E W 1"},
    {"%3$1n%#<E%(+tQ", "E W 1"},
    {"%1$1n%b", "E W 1"},
    {"%B%+#S%n%##tL", "E X s+"},
    {"%1$tQ% 1Tz%5b", "E X z "},
    {"%(5b%5B%3$12.0G", "E X b("},
    {"%3$+S% <H%12te%n%1B", "E X s+"},
    {"%2$1.2A%%%2$< tY", "E X Y "},
    {"%n%#d%3$5A", "E X d#"},
    {"%q", "E U q"},
    {"%", "E U %"},
    {"ab%", "E U %"},
    {"%0$d", "E I 0"},
    {"%99999999999$d", "E I -2147483648"},
    {"%tq", "E U tq"},
    {"%<s", "OK [%<s|0]"},
    {"%s%<s%<d", "OK [%s|1] [%<s|1] [%<d|1]"},
    {"%99999999999d", "E W -2147483648"},
    {"%.99999999999f", "E P -2147483648"},
    {"%2$s%s%<s%s", "OK [%2$s|2] [%s|1] [%<s|1] [%s|2]"},
    {"%-%", "E M %-%"},
    {"%5%", "OK [%5%|-2]"},
  };
  for(const auto &c : cases)
  {
    INFO("format string: " << c.first);
    REQUIRE(show(java_format_parse(c.first)) == c.second);
  }
}

TEST_CASE(
  "java_format_flagst::to_string in the order of Formatter",
  "[core][solvers][strings][format_specifier]")
{
  using namespace java_format_flagst;
  REQUIRE(to_string(0).empty());
  REQUIRE(to_string(parentheses | left_justify | alternate) == "-#(");
  REQUIRE(to_string(previous | zero_pad | plus) == "+0<");
}

// The expected results are those of Formatter of Java 8 (as of 8 to 15),
// 16 (as of 16 to 18) and 19 (as of 19 and later).
TEST_CASE(
  "java_format_parse of the versions of java.util.Formatter",
  "[core][solvers][strings][format_specifier]")
{
  using versiont = java_formatter_versiont;
  const auto parse = [](const std::string &s, versiont v)
  { return show(java_format_parse(s, v)); };
  // Java 8 to 15: an index of 0 is ordinary, and a number beyond int is
  // ignored (an index is then relative, here without a previous argument)
  REQUIRE(parse("%0$s", versiont::JAVA_8) == "OK [%s|1]");
  REQUIRE(parse("%99999999999$s", versiont::JAVA_8) == "OK [%s|0]");
  REQUIRE(parse("%s %99999999999$s", versiont::JAVA_8) == "OK [%s|1] [%s|1]");
  REQUIRE(parse("%99999999999d", versiont::JAVA_8) == "OK [%d|1]");
  REQUIRE(parse("%0$s", versiont::JAVA_16) == "E I 0");
  REQUIRE(parse("%99999999999d", versiont::JAVA_16) == "E W -2147483648");
  // the flags that do not fit: the first up to Java 18, all since 19
  REQUIRE(parse("%#(+tY", versiont::JAVA_16) == "E X Y#");
  REQUIRE(parse("%#(+tY", versiont::JAVA_19) == "E X Y#+(");
}
