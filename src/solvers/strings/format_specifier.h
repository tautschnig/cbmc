/*******************************************************************\

Module: Format specifiers for String.format

Author: Romain Brenguier, Joel Allred

\*******************************************************************/

/// \file
///  Format specifiers for String.format

#ifndef CPROVER_SOLVERS_STRINGS_FORMAT_SPECIFIER_H
#define CPROVER_SOLVERS_STRINGS_FORMAT_SPECIFIER_H

#include <util/invariant.h>

#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

// Format specifier describes how a value should be printed.
/// Field names follow the OpenJDK implementation:
/// http://hg.openjdk.java.net/jdk7/jdk7/jdk/file/9b8c96f96a0f/src/share/classes/java/util/Formatter.java#l2569
class format_specifiert
{
public:
  // Constants describing the meaning of characters in format specifiers.
  static const char DECIMAL_INTEGER = 'd';
  static const char OCTAL_INTEGER = 'o';
  static const char HEXADECIMAL_INTEGER = 'x';
  static const char HEXADECIMAL_INTEGER_UPPER = 'X';
  static const char SCIENTIFIC = 'e';
  static const char SCIENTIFIC_UPPER = 'E';
  static const char GENERAL = 'g';
  static const char GENERAL_UPPER = 'G';
  static const char DECIMAL_FLOAT = 'f';
  static const char HEXADECIMAL_FLOAT = 'a';
  static const char HEXADECIMAL_FLOAT_UPPER = 'A';
  static const char CHARACTER = 'c';
  static const char CHARACTER_UPPER = 'C';
  static const char DATE_TIME = 't';
  static const char DATE_TIME_UPPER = 'T';
  static const char BOOLEAN = 'b';
  static const char BOOLEAN_UPPER = 'B';
  static const char STRING = 's';
  static const char STRING_UPPER = 'S';
  static const char HASHCODE = 'h';
  static const char HASHCODE_UPPER = 'H';
  static const char LINE_SEPARATOR = 'n';
  static const char PERCENT_SIGN = '%';

  int index = -1;
  std::string flag;
  int width;
  int precision;
  // date/time
  bool dt = false;
  /// The field of a date/time conversion, as 'Q' for %tQ.
  char date_time_field = 0;
  char conversion;
  /// Whether the specifier cannot be modelled, as for a '<' flag without a
  /// previous argument.
  bool unsupported = false;
  /// Whether the conversion is not one of Java's (Java throws
  /// UnknownFormatConversionException).
  bool unknown_conversion = false;

  /// Whether the specifier is one that the string solver models.
  bool is_modelled() const
  {
    return !unsupported && !unknown_conversion;
  }

  /// Whether the specifier has flags, a width, or a precision but for a
  /// floating-point conversion (which its core takes, see
  /// java_format_decorate).
  bool has_decoration() const
  {
    const bool core_precision =
      !dt && std::string{"eEfgGaA"}.find(conversion) != std::string::npos;
    return !flag.empty() || width != -1 || (precision != -1 && !core_precision);
  }

  format_specifiert(
    int _index,
    std::string _flag,
    int _width,
    int _precision,
    bool _dt,
    char conversion)
    : index(_index),
      flag(_flag),
      width(_width),
      precision(_precision),
      dt(_dt),
      conversion(conversion)
  {
  }
};

// Format text represent a constant part of a format string.
class format_textt
{
public:
  format_textt() = default;

  explicit format_textt(std::string _content) : content(_content)
  {
  }

  format_textt(const format_textt &fs) : content(fs.content)
  {
  }

  std::string get_content() const
  {
    return content;
  }

private:
  std::string content;
};

// A format element is either a specifier or text.
class format_elementt
{
public:
  typedef enum
  {
    SPECIFIER,
    TEXT
  } format_typet;

  explicit format_elementt(format_typet _type) : type(_type)
  {
  }

  explicit format_elementt(std::string s) : type(TEXT), fstring(s)
  {
  }

  explicit format_elementt(format_specifiert fs) : type(SPECIFIER)
  {
    fspec.push_back(fs);
  }

  bool is_format_specifier() const
  {
    return type == SPECIFIER;
  }

  bool is_format_text() const
  {
    return type == TEXT;
  }

  format_specifiert get_format_specifier() const
  {
    PRECONDITION(is_format_specifier());
    return fspec.back();
  }

  format_textt &get_format_text()
  {
    PRECONDITION(is_format_text());
    return fstring;
  }

  const format_textt &get_format_text() const
  {
    PRECONDITION(is_format_text());
    return fstring;
  }

private:
  format_typet type;
  format_textt fstring;
  std::vector<format_specifiert> fspec;
};

/// Parse the given string into format specifiers and text.
/// This follows the implementation in openJDK of the java.util.Formatter class:
/// http://hg.openjdk.java.net/jdk7/jdk7/jdk/file/9b8c96f96a0f/src/share/classes/java/util/Formatter.java#l2513
/// \param s: a string
/// \return A vector of format_elementt.
std::vector<format_elementt> parse_format_string(std::string s);

/// A format specifier as java.util.Formatter parses it, for the checks of
/// its print step (see java_format_parse).
struct java_format_specifiert
{
  /// Whether the specifier takes an argument (not %n or %%).
  bool takes_argument = false;
  /// The argument, from 1, or 0 for a `<` without a previous argument.
  int argument = 0;
  /// The conversion, lower case unless it is the field of a date/time.
  char conversion = 0;
  bool date_time = false;
  /// The flags, as Formatter's Flags bits.
  int flags = 0;
  /// The width and the precision, -1 for none.
  int width = -1;
  int precision = -1;
  /// Formatter's toString of the specifier (for the exception messages).
  std::string text;
};

/// The result of java_format_parse: the error of the parsing, as a code
/// character and the detail of its message (see java_format_errort), or the
/// specifiers.
struct java_format_parset
{
  std::optional<std::pair<char, std::string>> error;
  std::vector<java_format_specifiert> specifiers;
};

/// The codes of the errors of String.format, with the detail of their
/// message: an exception of java.util and the argument of its constructor.
namespace java_format_errort
{
const char unknown_conversion = 'U';  // the conversion
const char argument_index = 'I';      // the index, decimal
const char duplicate_flags = 'D';     // the flag
const char width = 'W';               // the width, decimal
const char precision = 'P';           // the precision, decimal
const char illegal_flags = 'F';       // the flags
const char flags_mismatch = 'X';      // the conversion, then the flags
const char missing_width = 'M';       // the specifier
const char missing_argument = 'A';    // the specifier
const char conversion_mismatch = 'C'; // the conversion, then the argument
const char date_time_mismatch = 'T';  // the field, then the argument
const char code_point = 'K';          // the argument
const char division_by_zero = 'Z';    // the argument (grouping size 0)
} // namespace java_format_errort

/// Formatter's Flags bits.
namespace java_format_flagst
{
const int left_justify = 1 << 0;
const int uppercase = 1 << 1;
const int alternate = 1 << 2;
const int plus = 1 << 3;
const int leading_space = 1 << 4;
const int zero_pad = 1 << 5;
const int group = 1 << 6;
const int parentheses = 1 << 7;
const int previous = 1 << 8;
std::string to_string(int flags);
} // namespace java_format_flagst

/// The versions of java.util.Formatter whose parse of format strings
/// differ.
enum class java_formatter_versiont
{
  /// Java 8 to 15: an argument index of 0 is ordinary, and an index, width
  /// or precision beyond the range of int is ignored (the index is then
  /// relative, as with `<`).
  JAVA_8,
  /// Java 16 to 18: those throw, and a FormatFlagsConversionMismatch names
  /// the first flag that does not fit.
  JAVA_16,
  /// Java 19 and later: a FormatFlagsConversionMismatch names all the flags
  /// that do not fit.
  JAVA_19
};

/// The versions of Formatter of the Java runtimes that may run the program
/// (see configt::javat::may_run_on).
std::vector<java_formatter_versiont> java_formatter_versions();

/// Parse \p s as java.util.Formatter of \p version parses a format
/// string, with the checks that its specifiers make when they are parsed:
/// the first error, or the specifiers.
java_format_parset java_format_parse(
  const std::string &s,
  java_formatter_versiont version = java_formatter_versiont::JAVA_19);

/// The flags of \p flags among \p bad (in the order of Formatter's checks)
/// that Formatter of \p version reports in a
/// FormatFlagsConversionMismatchException: all of them since Java 19, the
/// first before. 0 for none.
int java_format_bad_flags(
  int flags,
  std::initializer_list<int> bad,
  java_formatter_versiont version);

/// The Formatter flags of the flag characters \p flags ("-#+ 0,(<"), with
/// UPPERCASE if \p conversion is an upper-case one.
int java_format_flags(const std::string &flags, char conversion);

/// The versions of java.util.Formatter whose printing of flags and widths
/// differs.
struct java_format_print_versiont
{
  /// Whether %% takes its width (since Java 11: not in Java 8 to 10).
  bool percent_width = true;
  /// Whether the zeros of %a count the sign (since Java 17).
  bool hex_float_counts_sign = true;
};

/// The versions of the Java runtimes that may run the program (see
/// configt::javat::may_run_on).
std::vector<java_format_print_versiont> java_format_print_versions();

/// What java.util.Formatter prints for a specifier, from its \p core: the
/// string of the conversion without flags, width or precision, in lower
/// case, as the string solver models it:
/// - for %d of a Byte, Short, Integer or Long, Long.toString of the value;
/// - for %o and %x of one, the digits of its unsigned value;
/// - for %e, %f, %g and %a of a Float or Double, java_format_float with the
///   precision;
/// - for the other conversions, and for null, the string that Formatter
///   prints (as "null"), before the precision and the case.
/// \param conversion: the conversion, in lower case ('t' for a date/time)
/// \param flags: the flags (see java_format_flagst), with UPPERCASE for an
///   upper-case conversion
/// \param width: the width, -1 for none
/// \param precision: the precision, -1 for none
/// \param numeric: whether the argument is a number that the conversion
///   prints as one (not null)
/// \return the string, or nothing where it depends on the locale (the
///   grouping of `,`, the case of other than ASCII)
std::optional<std::string> java_format_decorate(
  char conversion,
  int flags,
  int width,
  int precision,
  const std::string &core,
  bool numeric,
  java_format_print_versiont version = {});

/// The zeros that Formatter adds to the fraction of a %a with a precision:
/// it rounds the value to the precision and takes Double.toHexString of
/// it, whose fraction has no zeros at its end but for a single digit, then
/// pads the fraction to the precision. Its `0` flag counts the string
/// before that padding.
/// \param magnitude: the %a of a finite value with the precision, without
///   its sign (as "0x1.00p0", see java_format_float)
/// \param precision: the precision (0 prints one digit)
/// \return the number of zeros at the end of the fraction of \p magnitude
///   that the padding added
std::size_t java_hex_float_padding(const std::string &magnitude, int precision);

#endif // CPROVER_SOLVERS_STRINGS_FORMAT_SPECIFIER_H
