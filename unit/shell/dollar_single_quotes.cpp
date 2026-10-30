/// \file
/// Unit tests for decoding $'...' strings (POSIX.1-2024 XCU 2.2.4).

#include <util/message.h>
#include <util/source_location.h>

#include <shell/shfmt_reader.h>
#include <testing-utils/use_catch.h>

namespace
{
/// Decode \p raw; \p warnings receives the number of warnings issued.
std::string decode(const std::string &raw, std::size_t &warnings)
{
  null_message_handlert handler;
  messaget log{handler};
  const std::string result =
    decode_dollar_single_quotes(raw, source_locationt::nil(), log);
  warnings = handler.get_message_count(messaget::M_WARNING);
  return result;
}

std::string decode(const std::string &raw)
{
  std::size_t warnings;
  const std::string result = decode(raw, warnings);
  REQUIRE(warnings == 0);
  return result;
}

std::string decode_with_warning(const std::string &raw)
{
  std::size_t warnings;
  const std::string result = decode(raw, warnings);
  REQUIRE(warnings == 1);
  return result;
}
} // namespace

TEST_CASE("$'...': single-character escapes", "[shell]")
{
  REQUIRE(decode("plain text") == "plain text");
  REQUIRE(decode("\\\"\\'\\\\") == "\"'\\");
  REQUIRE(decode("\\a\\b\\e\\f\\n\\r\\t\\v") == "\a\b\x1b\f\n\r\t\v");
  // A double-quote needs no escape.
  REQUIRE(decode("\"") == "\"");
}

TEST_CASE("$'...': control characters", "[shell]")
{
  REQUIRE(decode("\\cA\\ca\\cZ") == "\x01\x01\x1a");
  REQUIRE(decode("\\c[\\c]\\c^\\c_") == "\x1b\x1d\x1e\x1f");
  REQUIRE(decode("\\c?") == "\x7f");
  REQUIRE(decode("\\c\\\\") == "\x1c");
  // Not in the stty ^c table.
  REQUIRE(decode_with_warning("\\c@") == "\\c@");
  REQUIRE(decode_with_warning("\\c1") == "\\c1");
  REQUIRE(decode_with_warning("\\c") == "\\c");
}

TEST_CASE("$'...': hexadecimal and octal escapes", "[shell]")
{
  REQUIRE(decode("\\x41\\x6a") == "Aj");
  REQUIRE(decode("\\x9z") == "\tz");
  REQUIRE(decode("\\101\\60\\0610") == "A010");
  REQUIRE(decode("\\1018") == "A8");
  REQUIRE(decode("\\7") == "\a");
  REQUIRE(decode_with_warning("\\x") == "\\x");
  REQUIRE(decode_with_warning("\\x414") == "\\x414");
  REQUIRE(decode_with_warning("\\x00") == "\\x00");
  REQUIRE(decode_with_warning("\\0") == "\\0");
  REQUIRE(decode_with_warning("\\777") == "\\777");
}

TEST_CASE("$'...': other escapes are unspecified", "[shell]")
{
  REQUIRE(decode_with_warning("\\q") == "\\q");
  REQUIRE(decode_with_warning("\\\n") == "\\\n");
}
