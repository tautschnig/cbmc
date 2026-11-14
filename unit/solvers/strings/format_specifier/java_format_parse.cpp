/*******************************************************************\

Module: Unit tests for java_format_parse

Author: Diffblue Ltd.

\*******************************************************************/

#include <solvers/strings/format_specifier.h>
#include <testing-utils/use_catch.h>

#include <cctype>
#include <regex>
#include <string>
#include <tuple>
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

// The expected strings are those of String.format of Java 25 (the same in
// Java 8, 11, 16, 17 and 21 but for the cases at the end), from the core
// (the string without flags, width or precision), as java_format_decorate
// takes it.
TEST_CASE(
  "java_format_decorate as java.util.Formatter",
  "[core][solvers][strings][format_specifier]")
{
  const std::regex specifier{"%([-#+ 0(,]*)(\\d*)(\\.\\d+)?([a-zA-Z%])"};
  const auto decorate = [&](
                          const std::string &spec,
                          const std::string &core,
                          bool numeric,
                          java_format_print_versiont version)
  {
    std::smatch m;
    REQUIRE(std::regex_match(spec, m, specifier));
    const char conversion = m[4].str()[0];
    return java_format_decorate(
      static_cast<char>(std::tolower(conversion)),
      java_format_flags(m[1].str(), conversion),
      m[2].str().empty() ? -1 : std::stoi(m[2].str()),
      m[3].str().empty() ? -1 : std::stoi(m[3].str().substr(1)),
      core,
      numeric,
      version);
  };
  const std::vector<std::tuple<std::string, std::string, bool, std::string>>
    cases{
      {"%-3%", "%", false, "%  "},
      {"%12%", "%", false, "           %"},
      {"%5%", "%", false, "    %"},
      {"%#.1A", "-0x0.0p0", true, "-0X0.0P0"},
      {"%+.12A", "0x1.52d02c7e14afp76", true, "+0X1.52D02C7E14AFP76"},
      {"% 25.1A", "0x1.5p76", true, "                 0X1.5P76"},
      {"%-3.1B", "false", false, "F  "},
      {"%.12B", "false", false, "FALSE"},
      {"%1.3B", "false", false, "FAL"},
      {"%5C", "%", false, "    %"},
      {"%12C", "%", false, "           %"},
      {"%25C", "a", false, "                        A"},
      {"%25.0E", "1e+06", true, "                    1E+06"},
      {"% 12.12E", "1.500000000000e+00", true, " 1.500000000000E+00"},
      {"%.1E", "2.5e+00", true, "2.5E+00"},
      {"%1.12G", "NaN", true, "NAN"},
      {"%05.12G", "-0.00000000000", true, "-0.00000000000"},
      {"%0( 25G", "Infinity", true, "                 INFINITY"},
      {"%3.12H", "ffffffff", false, "FFFFFFFF"},
      {"%3H", "ffffffff", false, "FFFFFFFF"},
      {"%1.0H", "1c6d1", false, " "},
      {"%25.0S", "null", false, "                         "},
      {"%-3S", "false", false, "FALSE"},
      {"%25S", "null", false, "                     NULL"},
      {"%0 3X", "null", false, "NULL"},
      {"%#12X", "null", false, "        NULL"},
      {"%12X", "2a", true, "          2A"},
      {"%25.0a", "NaN", true, "                      NaN"},
      {"%0+5a", "-0x1.8p0", true, "-0x1.8p0"},
      {"%12.12a", "0x1.b7cdfd9d7bdcp-34", true, "0x1.b7cdfd9d7bdcp-34"},
      {"%1b", "false", false, "false"},
      {"%b", "true", false, "true"},
      {"%.0b", "false", false, ""},
      {"%25c", "%", false, "                        %"},
      {"%12c", "%", false, "           %"},
      {"%-3c", "Z", false, "Z  "},
      {"%3d", "-9223372036854775808", true, "-9223372036854775808"},
      {"%3d", "-7", true, " -7"},
      {"%+-12d", "127", true, "+127        "},
      {"%1e", "NaN", true, "NaN"},
      {"% 12e", "-1.000000e-01", true, "-1.000000e-01"},
      {"%+12e", "1.000000e-10", true, "+1.000000e-10"},
      {"%(.12f", "1.500000000000", true, "1.500000000000"},
      {"%1f",
       "339999995214436420000000000000000000000.000000",
       true,
       "339999995214436420000000000000000000000.000000"},
      {"% 5.12f", "1.500000000000", true, " 1.500000000000"},
      {"%3.0g", "-0.1", true, "-0.1"},
      {"%3.3g", "Infinity", true, "Infinity"},
      {"%-(12g", "null", false, "null        "},
      {"%3.3h", "null", false, "nul"},
      {"%.12h", "80000000", false, "80000000"},
      {"%3h", "4cf", false, "4cf"},
      {"%#1o", "0", true, "00"},
      {"%25o", "null", false, "                     null"},
      {"% o", "null", false, "null"},
      {"%3.0s", "x y", false, "   "},
      {"%5.3s", "null", false, "  nul"},
      {"%12.0s", "a", false, "            "},
      {"%+#x", "null", false, "null"},
      {"%x", "1cbe991a14", true, "1cbe991a14"},
      {"%25x", "null", false, "                     null"},
    };
  for(const auto &c : cases)
  {
    INFO("specifier " << std::get<0>(c) << " of " << std::get<1>(c));
    REQUIRE(
      decorate(std::get<0>(c), std::get<1>(c), std::get<2>(c), {}) ==
      std::get<3>(c));
  }
  // Java 8 to 10 print %% without its width; the zeros of %a count the sign
  // since Java 17
  REQUIRE(decorate("%5%", "%", false, {false, false}) == "%");
  REQUIRE(decorate("%5%", "%", false, {true, false}) == "    %");
  REQUIRE(
    decorate("%+012A", "-0x0.0p0", true, {true, false}) == "-0X000000.0P0");
  REQUIRE(decorate("%+012A", "-0x0.0p0", true, {true, true}) == "-0X00000.0P0");
  // the zeros of %a with a precision count the string before the padding
  // of its fraction (Java 8 and 17)
  REQUIRE(
    decorate("%012.2a", "-0x1.00p0", true, {true, false}) == "-0x000001.00p0");
  REQUIRE(
    decorate("%012.2a", "-0x1.00p0", true, {true, true}) == "-0x00001.00p0");
  REQUIRE(
    decorate("%020.15a", "0x1.800000000000000p0", true, {true, true}) ==
    "0x00000000000001.800000000000000p0");
  REQUIRE(
    decorate("%012.12a", "0x1.921fb54442d2p1", true, {true, true}) ==
    "0x1.921fb54442d2p1");
  REQUIRE(java_hex_float_padding("0x1.00p0", 2) == 1);
  REQUIRE(java_hex_float_padding("0x1.80p0", 2) == 1);
  REQUIRE(java_hex_float_padding("0x1.0p0", 0) == 0);
  REQUIRE(java_hex_float_padding("0x1.92p1", 2) == 0);
  // the grouping depends on the locale
  REQUIRE_FALSE(decorate("%,d", "1234", true, {}).has_value());
  // null with the precision of a float
  REQUIRE(decorate("%.2e", "null", false, {}) == "nu");
}
