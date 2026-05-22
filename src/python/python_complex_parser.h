/// \file
/// Python complex-literal string parser.
///
/// Author: Diffblue Ltd.
///
/// Parses the string-form argument of Python's `complex(...)` call. CPython's
/// reference implementation lives in `Objects/complexobject.c`
/// (`complex_subtype_from_string`); we re-implement the surface grammar here
/// because the python-frontend folds constant `complex("...")` calls at
/// conversion time.
///
/// The previous implementation called `std::stod` in several places without
/// guards; on inputs like `complex("+j")` or `complex("x")` this threw
/// `std::invalid_argument` straight out of the frontend with no caller
/// catching it, aborting CBMC. The exception-free `std::strtod` plus an
/// "entire input consumed" check makes both well-formed and malformed
/// inputs explicit and side-effect-free.

#ifndef CPROVER_PYTHON_PYTHON_COMPLEX_PARSER_H
#define CPROVER_PYTHON_PYTHON_COMPLEX_PARSER_H

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

/// Parse a string in the format accepted by Python's `complex(...)`.
///
/// Returns `std::nullopt` for inputs that CPython's `complex()` would reject
/// with `ValueError`. Returns `(real, imag)` for inputs that `complex()`
/// would accept.
///
/// Surface grammar:
///
///     complex   := WS? ( '(' WS? body WS? ')' | body ) WS?
///     body      := pure_real | pure_imag | combined
///     pure_real := number
///     pure_imag := coeff_opt 'j'
///     combined  := number ('+' | '-') coeff_opt 'j'
///     coeff_opt := number | '+' | '-' | <empty>
///
/// where `number` is anything `strtod` accepts (decimal, signed,
/// scientific notation, and the case-insensitive specials `inf`,
/// `infinity`, `nan`). Internal whitespace inside `body` is rejected
/// (matching CPython, which only tolerates whitespace at the very
/// beginning / end and immediately inside the parentheses).
///
/// Examples:
///
///     "0"           -> (0.0, 0.0)
///     "-3.5"        -> (-3.5, 0.0)
///     "1e2"         -> (100.0, 0.0)
///     "j"           -> (0.0, 1.0)
///     "+j"          -> (0.0, 1.0)
///     "-j"          -> (0.0, -1.0)
///     "2j"          -> (0.0, 2.0)
///     "1+2j"        -> (1.0, 2.0)
///     "3+j"         -> (3.0, 1.0)
///     "1e3+2e-1j"   -> (1000.0, 0.2)
///     "(3+4j)"      -> (3.0, 4.0)
///     "  5  "       -> (5.0, 0.0)
///     ".5j"         -> (0.0, 0.5)
///     "x"           -> nullopt
///     "1 + 2j"      -> nullopt   // internal whitespace
///     "++1j"        -> nullopt
///     ""            -> nullopt
inline std::optional<std::pair<double, double>>
parse_python_complex_string(std::string_view sv)
{
  // Strip surrounding whitespace.
  auto strip_ws = [](std::string_view &v)
  {
    while(!v.empty() && std::isspace(static_cast<unsigned char>(v.front())))
      v.remove_prefix(1);
    while(!v.empty() && std::isspace(static_cast<unsigned char>(v.back())))
      v.remove_suffix(1);
  };
  strip_ws(sv);
  if(sv.empty())
    return std::nullopt;

  // Strip a single layer of parentheses, allowing whitespace inside.
  if(sv.front() == '(' && sv.back() == ')')
  {
    sv.remove_prefix(1);
    sv.remove_suffix(1);
    strip_ws(sv);
    if(sv.empty())
      return std::nullopt;
  }

  // After unwrapping, no whitespace is allowed inside the body.
  for(char c : sv)
    if(std::isspace(static_cast<unsigned char>(c)))
      return std::nullopt;

  // Parse one coefficient: a strtod-compatible substring, OR (for the
  // imaginary coefficient only) the empty string / "+" / "-", which mean
  // ±1.0. Insists on full consumption of the input by strtod, so
  // `strtod("3+", ...)` -> std::nullopt rather than 3.0 with junk left.
  auto parse_coeff =
    [](std::string_view s, bool allow_implicit_one) -> std::optional<double>
  {
    if(s.empty() || s == "+")
      return allow_implicit_one ? std::optional<double>{1.0} : std::nullopt;
    if(s == "-")
      return allow_implicit_one ? std::optional<double>{-1.0} : std::nullopt;

    std::string buf{s};
    errno = 0;
    char *endp = nullptr;
    const double v = std::strtod(buf.c_str(), &endp);
    if(endp != buf.c_str() + buf.size())
      return std::nullopt;
    // ERANGE on overflow gives ±HUGE_VAL and on underflow gives 0;
    // both match Python complex() semantics, so we just return v.
    return v;
  };

  const bool ends_j = sv.back() == 'j' || sv.back() == 'J';

  if(!ends_j)
  {
    auto r = parse_coeff(sv, /*allow_implicit_one=*/false);
    if(!r.has_value())
      return std::nullopt;
    return std::pair<double, double>{*r, 0.0};
  }

  // Drop the trailing 'j' before splitting.
  std::string_view rest = sv;
  rest.remove_suffix(1);

  // Find the rightmost '+'/'-' that's neither a leading sign nor an
  // exponent sign in scientific notation. That's the real/imag split.
  std::optional<std::size_t> split_idx;
  for(std::size_t i = rest.size(); i > 0; --i)
  {
    const char c = rest[i - 1];
    if(c != '+' && c != '-')
      continue;
    if(i == 1)
      continue; // leading sign
    const char prev = rest[i - 2];
    if(prev == 'e' || prev == 'E')
      continue; // scientific-notation sign
    split_idx = i - 1;
    break;
  }

  if(split_idx.has_value())
  {
    auto real_str = rest.substr(0, *split_idx);
    auto imag_str = rest.substr(*split_idx);
    auto r = parse_coeff(real_str, /*allow_implicit_one=*/false);
    auto im = parse_coeff(imag_str, /*allow_implicit_one=*/true);
    if(!r.has_value() || !im.has_value())
      return std::nullopt;
    return std::pair<double, double>{*r, *im};
  }

  // No split: the whole `rest` is the imaginary coefficient.
  auto im = parse_coeff(rest, /*allow_implicit_one=*/true);
  if(!im.has_value())
    return std::nullopt;
  return std::pair<double, double>{0.0, *im};
}

#endif
