/// \file
/// Shell patterns (POSIX.1-2024 XCU 2.14 Pattern Matching Notation), and
/// C code that matches them, for case and for ${x#pattern} and the other
/// pattern removal expansions.
///
/// A pattern is a sequence of elements: characters, ?, bracket
/// expressions, *, and the values of expansions in quotes, which match
/// themselves literally. Matching is by bytes, as in the C locale.

#ifndef CPROVER_SHELL_SHELL_PATTERN_H
#define CPROVER_SHELL_SHELL_PATTERN_H

#include "shell_ast.h"

#include <bitset>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

struct shell_pattern_elementt
{
  enum class kindt
  {
    /// The byte \ref character.
    CHARACTER,
    /// ?: any byte.
    ANY,
    /// A bracket expression: a byte in \ref set.
    SET,
    /// The value of expansion \ref dynamic_index, matched literally.
    DYNAMIC,
    /// *: any string.
    STAR
  };
  kindt kind = kindt::CHARACTER;
  unsigned char character = 0;
  std::bitset<256> set;
  std::size_t dynamic_index = 0;
};

struct shell_patternt
{
  std::vector<shell_pattern_elementt> elements;
  /// The expansions whose values are part of the pattern, in quotes. Each
  /// is a part of a word of the IR.
  std::vector<const shell_word_partt *> dynamic_parts;

  /// A text that identifies the pattern up to the values of its dynamic
  /// parts, to share matchers between equal patterns.
  std::string signature() const;

  /// Whether the pattern has no *, ?, or bracket expression: it then
  /// matches exactly the string it denotes.
  bool is_literal() const;
};

/// Compile \p word into a pattern for \p dialect.
/// \param [out] error: why the word is not supported, if it is not
std::optional<shell_patternt> compile_shell_pattern(
  const shell_wordt &word,
  shell_dialectt dialect,
  std::string &error);

/// Names of the matcher functions for a pattern, given the name prefix.
/// match(s, d...) is whether s matches; the remove_* functions return s
/// without the shortest or longest matching prefix or suffix.
struct shell_pattern_functionst
{
  std::string match;
  std::string remove_shortest_prefix;
  std::string remove_longest_prefix;
  std::string remove_shortest_suffix;
  std::string remove_longest_suffix;
};

shell_pattern_functionst shell_pattern_function_names(const std::string &);

/// C code of the matcher functions for \p pattern, which must have no
/// dynamic parts, named after \p prefix, for strings of at most
/// \p max_length bytes. Each function takes the subject string. Matching
/// is by a table over positions of the string and of the pattern, which
/// is unrolled, as the rest of the runtime.
std::string shell_pattern_matcher_source(
  const shell_patternt &pattern,
  const std::string &prefix,
  std::size_t max_length);

#endif // CPROVER_SHELL_SHELL_PATTERN_H
