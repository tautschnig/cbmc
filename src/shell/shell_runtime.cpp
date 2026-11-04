/// \file
/// Runtime support for converted shell scripts.

#include "shell_runtime.h"

#include <util/config.h>
#include <util/message.h>
#include <util/symbol_table_base.h>

#include <ansi-c/ansi_c_language.h>

#include <sstream>

namespace
{
/// Builds the runtime source text. Loops over string positions are
/// unrolled here, so the generated C has none.
class runtime_writert
{
public:
  explicit runtime_writert(std::size_t max_length) : max_length(max_length)
  {
  }

  std::string str()
  {
    type();
    nondet_string();
    environment();
    equal();
    concat();
    from_integer();
    test_integer();
    arith_integer();
    substring();
    affix(false);
    affix(true);
    return out.str();
  }

private:
  const std::size_t max_length;
  std::ostringstream out;

  /// Start a function body. The runtime's own accesses and arithmetic are
  /// correct by construction, so checks requested for the script (such as
  /// --bounds-check) are disabled in it, and its steps are hidden from
  /// traces, as in CBMC's C library.
  void begin(const std::string &signature)
  {
    shell_runtime_function_begin(out, signature);
  }

  void end()
  {
    shell_runtime_function_end(out);
  }

  /// s[start..start+length), which the caller keeps within s.
  void substring()
  {
    begin("struct " SHELL_STRING_TAG " " SHELL_RT_SUBSTRING
          "(struct " SHELL_STRING_TAG
          " s, unsigned long long start, unsigned long long length)");
    out << "  struct " SHELL_STRING_TAG " r;\n"
        << "  r.set = 1;\n"
        << "  r.length = length;\n";
    for(std::size_t i = 0; i < max_length; ++i)
    {
      out << "  r.data[" << i << "] = start + " << i << "ull < " << max_length
          << "ull ? s.data[start + " << i << "ull] : 0;\n";
    }
    out << "  return r;\n";
    end();
  }

  /// Whether p is a prefix (or, with suffix, a suffix) of s.
  void affix(bool suffix)
  {
    begin(
      std::string{"_Bool "} +
      (suffix ? SHELL_RT_HAS_SUFFIX : SHELL_RT_HAS_PREFIX) +
      "(struct " SHELL_STRING_TAG " s, struct " SHELL_STRING_TAG " p)");
    out << "  _Bool r = p.length <= s.length;\n";
    for(std::size_t i = 0; i < max_length; ++i)
    {
      const std::string index =
        suffix ? "s.length - p.length + " + std::to_string(i) + "ull"
               : std::to_string(i) + "ull";
      out << "  r = r & (" << i << "ull >= p.length | (" << index << " < "
          << max_length << "ull && s.data[" << index << "] == p.data[" << i
          << "]));\n";
    }
    out << "  return r;\n";
    end();
  }

  void type()
  {
    out << "struct " SHELL_STRING_TAG "\n"
        << "{\n"
        << "  _Bool set;\n"
        << "  unsigned long long length;\n"
        << "  unsigned char data[" << max_length << "];\n"
        << "};\n\n";
  }

  /// A set string of nondeterministic contents without null bytes, as in
  /// the environment, arguments, or command output (after removal of null
  /// bytes, which shells do not store in variables).
  void nondet_string()
  {
    begin("struct " SHELL_STRING_TAG " " SHELL_RT_NONDET_STRING "(void)");
    // One assumption, as traces show each one.
    out << "  struct " SHELL_STRING_TAG " s;\n"
        << "  " CPROVER_PREFIX "assume(s.length <= " << max_length << "ull";
    for(std::size_t i = 0; i < max_length; ++i)
    {
      out << " &&\n    (" << i << "ull >= s.length || s.data[" << i
          << "] != 0)";
    }
    out << ");\n"
        << "  s.set = 1;\n"
        << "  return s;\n";
    end();
  }

  /// A variable as inherited from the environment: set or unset.
  void environment()
  {
    begin("struct " SHELL_STRING_TAG " " SHELL_RT_ENVIRONMENT "(void)");
    out << "  struct " SHELL_STRING_TAG " s = " SHELL_RT_NONDET_STRING "();\n"
        << "  _Bool set;\n"
        << "  s.set = set;\n"
        << "  if(!set)\n"
        << "    s.length = 0;\n"
        << "  return s;\n";
    end();
  }

  /// Equality of the contents; the `set` flags are not compared.
  void equal()
  {
    begin("_Bool " SHELL_RT_EQUAL "(struct " SHELL_STRING_TAG
          " a, struct " SHELL_STRING_TAG " b)");
    out << "  _Bool r = a.length == b.length;\n";
    for(std::size_t i = 0; i < max_length; ++i)
    {
      out << "  r = r & (" << i << "ull >= a.length | a.data[" << i
          << "] == b.data[" << i << "]);\n";
    }
    out << "  return r;\n";
    end();
  }

  /// Concatenation. The caller checks that the result fits.
  void concat()
  {
    begin("struct " SHELL_STRING_TAG " " SHELL_RT_CONCAT
          "(struct " SHELL_STRING_TAG " a, struct " SHELL_STRING_TAG " b)");
    out << "  struct " SHELL_STRING_TAG " r;\n"
        << "  r.set = 1;\n"
        << "  r.length = a.length + b.length;\n";
    for(std::size_t i = 0; i < max_length; ++i)
    {
      out << "  r.data[" << i << "] = " << i << "ull < a.length ? a.data[" << i
          << "] : b.data[" << i << "ull - a.length];\n";
    }
    out << "  return r;\n";
    end();
  }

  /// The decimal representation of an integer, as in $((...)) or $?.
  void from_integer()
  {
    begin("struct " SHELL_STRING_TAG " " SHELL_RT_FROM_INTEGER "(long long v)");
    out << "  struct " SHELL_STRING_TAG " r;\n"
        << "  r.set = 1;\n"
        << "  _Bool negative = v < 0;\n"
        << "  unsigned long long u = negative ? 0ull - (unsigned long long)v"
        << " : (unsigned long long)v;\n"
        << "  unsigned char d[20];\n"
        << "  unsigned long long n = 1;\n"
        << "  d[0] = '0' + u % 10ull;\n"
        << "  u = u / 10ull;\n";
    for(unsigned k = 1; k < 20; ++k)
    {
      out << "  if(u != 0)\n"
          << "  {\n"
          << "    d[" << k << "] = '0' + u % 10ull;\n"
          << "    u = u / 10ull;\n"
          << "    n = " << k + 1 << ";\n"
          << "  }\n";
    }
    out << "  r.length = n + negative;\n"
        << "  if(negative)\n"
        << "    r.data[0] = '-';\n";
    for(unsigned j = 0; j < 20; ++j)
    {
      out << "  if(" << j << "ull < n)\n"
          << "    r.data[" << j << "ull + negative] = d[n - " << j + 1
          << "ull];\n";
    }
    out << "  return r;\n";
    end();
  }

  /// Accumulate digit \p digit in base \p base into m, detecting values
  /// above 2^63 (the magnitude of LLONG_MIN).
  void accumulate(unsigned base, const std::string &digit)
  {
    const unsigned long long limit = (1ull << 63) / base;
    out << "      if(m > " << limit << "ull)\n"
        << "        overflow = 1;\n"
        << "      else\n"
        << "      {\n"
        << "        m = m * " << base << "ull + (" << digit << ");\n"
        << "        if(m > 9223372036854775808ull)\n"
        << "          overflow = 1;\n"
        << "      }\n";
  }

  /// Store the signed result of m and negative in *value.
  void store_result()
  {
    out << "  if(!negative && m == 9223372036854775808ull)\n"
        << "    return 0;\n"
        << "  *value = negative && m != 0 ? -(long long)(m - 1ull) - 1"
        << " : (long long)m;\n"
        << "  return 1;\n";
  }

  /// An operand of the test utility's integer comparisons (dash and bash):
  /// optional white space, an optional sign, decimal digits, and optional
  /// white space as \p trailing says: none (0), space and tab (1), or
  /// isspace() bytes (2), the values of trailing_blankst. Values outside
  /// the range of long long are invalid.
  void test_integer()
  {
    begin("_Bool " SHELL_RT_TEST_INTEGER "(struct " SHELL_STRING_TAG
          " s, unsigned char trailing, long long *value)");
    out << "  unsigned char state = 0;\n"
        << "  _Bool negative = 0;\n"
        << "  _Bool overflow = 0;\n"
        << "  unsigned long long m = 0;\n";
    for(std::size_t i = 0; i < max_length; ++i)
    {
      out << "  if(" << i << "ull < s.length)\n"
          << "  {\n"
          << "    unsigned char c = s.data[" << i << "];\n"
          << "    _Bool space = c == ' ' || (c >= '\\t' && c <= '\\r');\n"
          << "    _Bool digit = c >= '0' && c <= '9';\n"
          << "    if(state == 0 && space)\n"
          << "      ;\n"
          << "    else if(state == 0 && (c == '-' || c == '+'))\n"
          << "    {\n"
          << "      negative = c == '-';\n"
          << "      state = 1;\n"
          << "    }\n"
          << "    else if(state <= 2 && digit)\n"
          << "    {\n"
          << "      state = 2;\n";
      accumulate(10, "c - '0'");
      out << "    }\n"
          << "    else if((state == 2 || state == 3) && (trailing == 2 ? "
             "space : trailing == 1 && (c == ' ' || c == '\\t')))\n"
          << "      state = 3;\n"
          << "    else\n"
          << "      state = 4;\n"
          << "  }\n";
    }
    out << "  if((state != 2 && state != 3) || overflow)\n"
        << "    return 0;\n";
    store_result();
    end();
  }

  /// The value of a variable in arithmetic expansion: an ISO C integer
  /// constant (decimal, octal if \p leading_zero_octal, or hexadecimal)
  /// with an optional sign and surrounding white space. White space is
  /// isspace() if \p any_space, else only space, tab, and newline. The
  /// empty string is 0.
  void arith_integer()
  {
    // States: 0 leading space, 1 after sign, 2 after a leading 0,
    // 3 after 0x, 4 hexadecimal, 5 octal, 6 decimal, 7 trailing space,
    // 8 invalid.
    begin("_Bool " SHELL_RT_ARITH_INTEGER "(struct " SHELL_STRING_TAG
          " s, _Bool leading_zero_octal, _Bool any_space, long long *value)");
    out << "  unsigned char state = 0;\n"
        << "  _Bool negative = 0;\n"
        << "  _Bool overflow = 0;\n"
        << "  unsigned long long m = 0;\n"
        << "  if(s.length == 0)\n"
        << "  {\n"
        << "    *value = 0;\n"
        << "    return 1;\n"
        << "  }\n";
    for(std::size_t i = 0; i < max_length; ++i)
      arith_integer_step(i);
    out << "  if(overflow || state == 0 || state == 1 || state == 3 ||"
           " state == 8)\n"
        << "    return 0;\n";
    store_result();
    end();
  }

  void arith_integer_step(std::size_t i)
  {
    out << "  if(" << i << "ull < s.length)\n"
        << "  {\n"
        << "    unsigned char c = s.data[" << i << "];\n"
        << "    _Bool space = c == ' ' || c == '\\t' || c == '\\n' ||\n"
        << "      (any_space && (c == '\\v' || c == '\\f' || c == '\\r'));\n"
        << "    _Bool digit = c >= '0' && c <= '9';\n"
        << "    _Bool odigit = c >= '0' && c <= '7';\n"
        << "    _Bool xdigit = digit || (c >= 'a' && c <= 'f') ||"
           " (c >= 'A' && c <= 'F');\n"
        << "    unsigned xvalue = digit ? c - '0' : (c | 0x20) - 'a' + 10;\n"
        << "    if(state == 0 && space)\n"
        << "      ;\n"
        << "    else if(state == 0 && (c == '-' || c == '+'))\n"
        << "    {\n"
        << "      negative = c == '-';\n"
        << "      state = 1;\n"
        << "    }\n"
        << "    else if(state <= 1 && c == '0')\n"
        << "      state = 2;\n"
        << "    else if(state == 2 && (c == 'x' || c == 'X'))\n"
        << "      state = 3;\n"
        << "    else if((state == 3 || state == 4) && xdigit)\n"
        << "    {\n"
        << "      state = 4;\n";
    accumulate(16, "xvalue");
    out << "    }\n"
        << "    else if((state == 2 || state == 5) && odigit &&"
           " leading_zero_octal)\n"
        << "    {\n"
        << "      state = 5;\n";
    accumulate(8, "c - '0'");
    out << "    }\n"
        << "    else if((state <= 1 || state == 6 ||\n"
        << "             (state == 2 && !leading_zero_octal)) && digit)\n"
        << "    {\n"
        << "      state = 6;\n";
    accumulate(10, "c - '0'");
    out << "    }\n"
        << "    else if(state >= 2 && state != 3 && state <= 7 && space)\n"
        << "      state = 7;\n"
        << "    else\n"
        << "      state = 8;\n"
        << "  }\n";
  }
};
} // namespace

void shell_runtime_function_begin(
  std::ostream &out,
  const std::string &signature)
{
  // The runtime's own accesses and arithmetic are correct by
  // construction, so checks requested for the script (such as
  // --bounds-check) are disabled in it, and its steps are hidden from
  // traces, as in CBMC's C library.
  out << signature << "\n"
      << "{\n"
      << CPROVER_PREFIX "HIDE:;\n"
      << "#pragma CPROVER check push\n";
  for(const char *check :
      {"bounds",
       "pointer",
       "pointer-primitive",
       "pointer-overflow",
       "signed-overflow",
       "unsigned-overflow",
       "undefined-shift",
       "conversion",
       "div-by-zero"})
  {
    out << "#pragma CPROVER check disable \"" << check << "\"\n";
  }
}

void shell_runtime_function_end(std::ostream &out)
{
  out << "#pragma CPROVER check pop\n"
      << "}\n\n";
}

std::string shell_runtime_declarations(std::size_t max_length)
{
  return "struct " SHELL_STRING_TAG
         "\n{\n  _Bool set;\n"
         "  unsigned long long length;\n  unsigned char data[" +
         std::to_string(max_length) +
         "];\n};\n"
         "struct " SHELL_STRING_TAG " " SHELL_RT_SUBSTRING
         "(struct " SHELL_STRING_TAG
         " s, unsigned long long start, unsigned long long length);\n";
}

std::string shell_runtime_source(std::size_t max_length)
{
  return runtime_writert{max_length}.str();
}

std::set<irep_idt> shell_runtime_symbols()
{
  return {
    "tag-" SHELL_STRING_TAG,
    SHELL_RT_NONDET_STRING,
    SHELL_RT_ENVIRONMENT,
    SHELL_RT_EQUAL,
    SHELL_RT_CONCAT,
    SHELL_RT_FROM_INTEGER,
    SHELL_RT_TEST_INTEGER,
    SHELL_RT_ARITH_INTEGER,
    SHELL_RT_SUBSTRING,
    SHELL_RT_HAS_PREFIX,
    SHELL_RT_HAS_SUFFIX};
}

bool add_shell_runtime_code(
  const std::string &source,
  const std::set<irep_idt> &functions,
  symbol_table_baset &symbol_table,
  message_handlert &message_handler)
{
  // The runtime uses no preprocessor directives; do not require a C
  // preprocessor for shell scripts.
  const auto preprocessor = config.ansi_c.preprocessor;
  config.ansi_c.preprocessor = configt::ansi_ct::preprocessort::NONE;

  std::istringstream in{source};
  ansi_c_languaget c_language;
  std::set<irep_idt> keep = functions;
  keep.insert("tag-" SHELL_STRING_TAG);
  const bool error =
    c_language.parse(in, "", message_handler) ||
    c_language.typecheck(
      symbol_table, "<shell-runtime>", message_handler, true, keep);

  config.ansi_c.preprocessor = preprocessor;

  if(error)
  {
    messaget log{message_handler};
    log.error() << "failed to add the shell runtime" << messaget::eom;
  }
  return error;
}

bool add_shell_runtime(
  std::size_t max_length,
  symbol_table_baset &symbol_table,
  message_handlert &message_handler)
{
  if(symbol_table.has_symbol(SHELL_RT_EQUAL))
    return false;
  return add_shell_runtime_code(
    shell_runtime_source(max_length),
    shell_runtime_symbols(),
    symbol_table,
    message_handler);
}
