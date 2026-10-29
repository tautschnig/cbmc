/// \file
/// Unit tests for shell dialect detection.

#include <shell/shell_dialect.h>
#include <testing-utils/use_catch.h>

TEST_CASE("shell dialect: #! line", "[shell]")
{
  REQUIRE(detect_shell_dialect("#!/bin/sh\n", "x") == shell_dialectt::POSIX);
  REQUIRE(detect_shell_dialect("#!/bin/dash\n", "x") == shell_dialectt::POSIX);
  REQUIRE(detect_shell_dialect("#!/bin/bash\n", "x") == shell_dialectt::BASH);
  REQUIRE(
    detect_shell_dialect("#! /usr/local/bin/bash -e\n", "x") ==
    shell_dialectt::BASH);
  REQUIRE(detect_shell_dialect("#!/bin/zsh", "x") == shell_dialectt::ZSH);
  REQUIRE(detect_shell_dialect("#!/bin/mksh\n", "x") == shell_dialectt::MKSH);
  REQUIRE(detect_shell_dialect("#!/bin/ksh\n", "x") == shell_dialectt::MKSH);
  REQUIRE(detect_shell_dialect("#!/bin/bash\r\n", "x") == shell_dialectt::BASH);
}

TEST_CASE("shell dialect: env", "[shell]")
{
  REQUIRE(
    detect_shell_dialect("#!/usr/bin/env bash\n", "x") == shell_dialectt::BASH);
  REQUIRE(
    detect_shell_dialect("#!/usr/bin/env -S zsh -f\n", "x") ==
    shell_dialectt::ZSH);
  REQUIRE(
    detect_shell_dialect("#!/usr/bin/env sh\n", "x.bash") ==
    shell_dialectt::POSIX);
}

TEST_CASE("shell dialect: file name extension", "[shell]")
{
  REQUIRE(detect_shell_dialect("echo\n", "a.bash") == shell_dialectt::BASH);
  REQUIRE(detect_shell_dialect("echo\n", "a.zsh") == shell_dialectt::ZSH);
  REQUIRE(detect_shell_dialect("echo\n", "a.ksh") == shell_dialectt::MKSH);
  REQUIRE(detect_shell_dialect("echo\n", "a.mksh") == shell_dialectt::MKSH);
  REQUIRE(detect_shell_dialect("echo\n", "a.sh") == shell_dialectt::POSIX);
  REQUIRE(detect_shell_dialect("", "a") == shell_dialectt::POSIX);
  // The #! line takes precedence.
  REQUIRE(
    detect_shell_dialect("#!/bin/sh\n", "a.bash") == shell_dialectt::POSIX);
}

TEST_CASE("shell dialect: arithmetic options", "[shell]")
{
  const auto posix = arith_options(shell_dialectt::POSIX);
  REQUIRE(posix.increment_mode == shell_arith_increment_modet::NONE);
  REQUIRE_FALSE(posix.comma_operator);

  const auto bash = arith_options(shell_dialectt::BASH);
  REQUIRE(bash.increment_mode == shell_arith_increment_modet::LENIENT);
  REQUIRE(bash.comma_operator);

  for(auto dialect : {shell_dialectt::MKSH, shell_dialectt::ZSH})
  {
    const auto options = arith_options(dialect);
    REQUIRE(options.increment_mode == shell_arith_increment_modet::STRICT);
    REQUIRE(options.comma_operator);
  }
}
