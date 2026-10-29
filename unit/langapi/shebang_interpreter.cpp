/// \file
/// Unit tests for shebang_interpreter.

#include <langapi/mode.h>
#include <testing-utils/use_catch.h>

/// The bytes of a string literal that contains null characters.
template <std::size_t N>
static std::string bytes(const char (&literal)[N])
{
  return std::string{literal, N - 1};
}

TEST_CASE("shebang_interpreter: plain #! lines", "[core][langapi]")
{
  REQUIRE(shebang_interpreter("#!/bin/sh\necho") == "sh");
  REQUIRE(shebang_interpreter("#!/bin/bash") == "bash");
  REQUIRE(shebang_interpreter("#! /usr/local/bin/bash -e\n") == "bash");
  REQUIRE(shebang_interpreter("#!\t/bin/zsh -f\n") == "zsh");
  REQUIRE(shebang_interpreter("#!/bin/sh\r\n") == "sh");
  REQUIRE(shebang_interpreter("#!python3\n") == "python3");
}

TEST_CASE("shebang_interpreter: env", "[core][langapi]")
{
  REQUIRE(shebang_interpreter("#!/usr/bin/env bash\n") == "bash");
  REQUIRE(shebang_interpreter("#!/usr/bin/env -S zsh -f\n") == "zsh");
  REQUIRE(shebang_interpreter("#!/usr/bin/env -i PATH=/bin sh\n") == "sh");
  REQUIRE(
    shebang_interpreter("#!/usr/bin/env /usr/bin/python3.12\n") ==
    "python3.12");
  REQUIRE(shebang_interpreter("#!/usr/bin/env\n").empty());
}

TEST_CASE("shebang_interpreter: no #! line", "[core][langapi]")
{
  REQUIRE(shebang_interpreter("").empty());
  REQUIRE(shebang_interpreter("#").empty());
  REQUIRE(shebang_interpreter("int main() {}\n").empty());
  REQUIRE(shebang_interpreter(" #!/bin/sh\n").empty());
  REQUIRE(shebang_interpreter("\n#!/bin/sh\n").empty());
  REQUIRE(shebang_interpreter("#!").empty());
}

TEST_CASE("shebang_interpreter: byte order marks", "[core][langapi]")
{
  REQUIRE(shebang_interpreter("\xEF\xBB\xBF#!/bin/sh\n") == "sh");

  const char utf16le[] = "\xFF\xFE#\0!\0/\0s\0h\0\n\0";
  REQUIRE(shebang_interpreter(bytes(utf16le)) == "sh");
  const char utf16be[] = "\xFE\xFF\0#\0!\0/\0s\0h\0\n";
  REQUIRE(shebang_interpreter(bytes(utf16be)) == "sh");

  const char utf32le[] = "\xFF\xFE\0\0#\0\0\0!\0\0\0s\0\0\0h\0\0\0";
  REQUIRE(shebang_interpreter(bytes(utf32le)) == "sh");
  const char utf32be[] = "\0\0\xFE\xFF\0\0\0#\0\0\0!\0\0\0s\0\0\0h";
  REQUIRE(shebang_interpreter(bytes(utf32be)) == "sh");

  // Without a byte order mark, UTF-16 is not recognised.
  const char utf16le_no_bom[] = "#\0!\0s\0h\0";
  REQUIRE(shebang_interpreter(bytes(utf16le_no_bom)).empty());
  // A truncated code unit ends the line.
  const char truncated[] = "\xFF\xFE#\0!\0s\0h";
  REQUIRE(shebang_interpreter(bytes(truncated)) == "s");
}
