/*******************************************************************\

Module: Unit tests for length_for_format_specifier
        solvers/refinement/string_format_builtin_function.cpp

Author: Diffblue Ltd.

\*******************************************************************/

#include <util/pointer_expr.h>
#include <util/range.h>
#include <util/simplify_expr.h>
#include <util/string_expr.h>

#include <solvers/strings/format_specifier.h>
#include <solvers/strings/string_format_builtin_function.h>
#include <testing-utils/empty_namespace.h>
#include <testing-utils/use_catch.h>

// Create array_string_exprt from array of characters
array_string_exprt from_char_vector(
  std::vector<mp_integer> string_array,
  const typet &char_type,
  const typet &length_type,
  const pointer_typet &pointer_type,
  array_poolt &array_pool)
{
  const array_typet array_type(
    char_type, from_integer(string_array.size(), length_type));
  const std::vector<exprt> expr_array =
    make_range(string_array).map([&](const mp_integer el) {
      return from_integer(el, char_type);
    });
  const array_exprt array(expr_array, array_type);
  const exprt first_element = index_exprt(array, from_integer(0, length_type));
  const exprt pointer = address_of_exprt(first_element, pointer_type);
  const array_string_exprt arg = array_pool.find(pointer, array_type.size());
  array_pool.insert(pointer, arg); // really necessary?
  return arg;
}

// The encoding of an argument of String.format by cproverFormatArgument of
// the Java models: its kind, its value as a long in four 16-bit characters,
// its hash code in two (zero here), its properties (none here) and its
// text.
static std::vector<mp_integer>
encoded(char kind, std::vector<mp_integer> value, const std::string &text)
{
  std::vector<mp_integer> result{kind};
  result.insert(result.end(), value.begin(), value.end());
  result.insert(result.end(), {0, 0, 0});
  for(const char c : text)
    result.push_back(c);
  return result;
}

SCENARIO(
  "length_for_format_specifier",
  "[core][solvers][strings][string_format_builtin_function]")
{
  const typet char_type = unsignedbv_typet(16);
  const typet length_type = signedbv_typet(32);
  const std::size_t pointer_width = 16;
  const auto pointer_type = pointer_typet(char_type, pointer_width);

  const namespacet &ns = empty_namespace;
  symbol_generatort fresh_symbol;
  array_poolt array_pool{fresh_symbol};

  WHEN("format specifier = %s")
  {
    WHEN("arg = encoded(\"bar\")")
    {
      array_string_exprt arg = from_char_vector(
        encoded('S', {0, 0, 0, 0}, "bar"),
        char_type,
        length_type,
        pointer_type,
        array_pool);
      format_specifiert fs{0, "", 0, 0, false, format_specifiert::STRING};
      const exprt actual =
        length_for_format_specifier(fs, arg, length_type, array_pool).value();
      THEN("length constraint represents 3")
      {
        int to_int =
          *numeric_cast<int>(to_constant_expr(simplify_expr(actual, ns)));
        REQUIRE(to_int == 3);
      }
    }
  }

  WHEN("format specifier = %S")
  {
    WHEN("arg = encoded(\"bar\")")
    {
      array_string_exprt arg = from_char_vector(
        encoded('S', {0, 0, 0, 0}, "bar"),
        char_type,
        length_type,
        pointer_type,
        array_pool);
      format_specifiert fs{0, "", 0, 0, false, format_specifiert::STRING_UPPER};
      const exprt actual =
        length_for_format_specifier(fs, arg, length_type, array_pool).value();
      THEN("length constraint represents 3")
      {
        int to_int =
          *numeric_cast<int>(to_constant_expr(simplify_expr(actual, ns)));
        REQUIRE(to_int == 3);
      }
    }
  }

  WHEN("format specifier = %s")
  {
    WHEN("arg = encoded(12)")
    {
      // %s prints the text of the argument, not its value characters
      array_string_exprt arg = from_char_vector(
        encoded('I', {0, 0, 0, 12}, "12"),
        char_type,
        length_type,
        pointer_type,
        array_pool);
      format_specifiert fs{0, "", 0, 0, false, format_specifiert::STRING};
      const exprt actual = simplify_expr(
        length_for_format_specifier(fs, arg, length_type, array_pool).value(),
        ns);
      THEN("length constraint represents 2")
      {
        REQUIRE(*numeric_cast<int>(to_constant_expr(actual)) == 2);
      }
    }
  }

  WHEN("format specifier = %b")
  {
    WHEN("arg = encoded(0), an Integer")
    {
      // only null and Boolean.FALSE are false
      array_string_exprt arg = from_char_vector(
        encoded('I', {0, 0, 0, 0}, "0"),
        char_type,
        length_type,
        pointer_type,
        array_pool);
      format_specifiert fs{0, "", 0, 0, false, format_specifiert::BOOLEAN};
      const exprt actual = simplify_expr(
        length_for_format_specifier(fs, arg, length_type, array_pool).value(),
        ns);
      THEN("length constraint represents 4")
      {
        REQUIRE(*numeric_cast<int>(to_constant_expr(actual)) == 4);
      }
    }
  }

  WHEN("format specifier = %c")
  {
    WHEN("arg = encoded(0x1F600), an Integer")
    {
      // a code point beyond the basic plane is a surrogate pair
      array_string_exprt arg = from_char_vector(
        encoded('I', {0, 0, 1, 0xF600}, "128512"),
        char_type,
        length_type,
        pointer_type,
        array_pool);
      format_specifiert fs{0, "", 0, 0, false, format_specifiert::CHARACTER};
      const exprt actual = simplify_expr(
        length_for_format_specifier(fs, arg, length_type, array_pool).value(),
        ns);
      THEN("length constraint represents 2")
      {
        REQUIRE(*numeric_cast<int>(to_constant_expr(actual)) == 2);
      }
    }
  }

  WHEN("format specifier = %E")
  {
    const array_string_exprt arg = from_char_vector(
      encoded('I', {0, 0, 0, 12}, "12"),
      char_type,
      length_type,
      pointer_type,
      array_pool);
    format_specifiert fs{
      0, "", 0, 0, false, format_specifiert::SCIENTIFIC_UPPER};
    THEN("there is no length formula")
    {
      REQUIRE_FALSE(
        length_for_format_specifier(fs, arg, length_type, array_pool)
          .has_value());
    }
  }

  WHEN("format specifier = %d")
  {
    WHEN("arg = encoded(12)")
    {
      const array_string_exprt arg = from_char_vector(
        encoded('I', {0, 0, 0, 12}, "12"),
        char_type,
        length_type,
        pointer_type,
        array_pool);
      format_specifiert fs{
        0, "", 0, 0, false, format_specifiert::DECIMAL_INTEGER};
      const exprt actual =
        length_for_format_specifier(fs, arg, length_type, array_pool).value();
      const exprt simpl_actual = simplify_expr(actual, ns);
      THEN("length constraint represents 2")
      {
        const int to_int = *numeric_cast<int>(to_constant_expr(simpl_actual));
        REQUIRE(to_int == 2);
      }
    }
  }

  WHEN("format specifier = %d")
  {
    WHEN("arg = encoded(-12)")
    {
      const array_string_exprt arg = from_char_vector(
        encoded('I', {0xFFFF, 0xFFFF, 0xFFFF, 0xFFF4}, "-12"),
        char_type,
        length_type,
        pointer_type,
        array_pool);
      format_specifiert fs{
        0, "", 0, 0, false, format_specifiert::DECIMAL_INTEGER};
      const exprt actual =
        length_for_format_specifier(fs, arg, length_type, array_pool).value();
      const exprt simpl_actual = simplify_expr(actual, ns);
      THEN("length constraint represents 2")
      {
        const int to_int = *numeric_cast<int>(to_constant_expr(simpl_actual));
        REQUIRE(to_int == 3);
      }
    }
  }

  WHEN("format specifier = %d")
  {
    WHEN("arg = encoded(-1234567890)")
    {
      const array_string_exprt arg = from_char_vector(
        encoded('I', {0xFFFF, 0xFFFF, 0xB669, 0xFD2E}, "-1234567890"),
        char_type,
        length_type,
        pointer_type,
        array_pool);
      format_specifiert fs{
        0, "", 0, 0, false, format_specifiert::DECIMAL_INTEGER};
      const exprt actual =
        length_for_format_specifier(fs, arg, length_type, array_pool).value();
      const exprt simpl_actual = simplify_expr(actual, ns);
      THEN("length constraint represents 11")
      {
        const int to_int = *numeric_cast<int>(to_constant_expr(simpl_actual));
        REQUIRE(to_int == 11);
      }
    }
  }

  WHEN("format specifier = %d")
  {
    WHEN("arg = encoded(null)")
    {
      const array_string_exprt arg = from_char_vector(
        encoded('N', {0, 0, 0, 0}, "null"),
        char_type,
        length_type,
        pointer_type,
        array_pool);
      format_specifiert fs{
        0, "", 0, 0, false, format_specifiert::DECIMAL_INTEGER};
      const exprt actual =
        length_for_format_specifier(fs, arg, length_type, array_pool).value();
      const exprt simpl_actual = simplify_expr(actual, ns);
      THEN("length constraint represents 4")
      {
        const int to_int = *numeric_cast<int>(to_constant_expr(simpl_actual));
        REQUIRE(to_int == 4);
      }
    }
  }

  WHEN("format specifier = %x")
  {
    WHEN("arg = encoded(12)")
    {
      const array_string_exprt arg = from_char_vector(
        encoded('I', {0, 0, 0, 12}, "12"),
        char_type,
        length_type,
        pointer_type,
        array_pool);
      format_specifiert fs{
        0, "", 0, 0, false, format_specifiert::HEXADECIMAL_INTEGER};
      const exprt actual =
        length_for_format_specifier(fs, arg, length_type, array_pool).value();
      const exprt simpl_actual = simplify_expr(actual, ns);
      THEN("length constraint represents 1")
      {
        const int to_int = *numeric_cast<int>(to_constant_expr(simpl_actual));
        REQUIRE(to_int == 1);
      }
    }
  }

  WHEN("format specifier = %c")
  {
    WHEN("arg = encoded('c')")
    {
      const array_string_exprt arg = from_char_vector(
        encoded('C', {0, 0, 0, 'c'}, "c"),
        char_type,
        length_type,
        pointer_type,
        array_pool);
      format_specifiert fs{0, "", 0, 0, false, format_specifiert::CHARACTER};
      const exprt actual =
        length_for_format_specifier(fs, arg, length_type, array_pool).value();
      const exprt simpl_actual = simplify_expr(actual, ns);
      THEN("length constraint represents 1")
      {
        const int to_int = *numeric_cast<int>(to_constant_expr(simpl_actual));
        REQUIRE(to_int == 1);
      }
    }
  }

  WHEN("format specifier = %c")
  {
    WHEN("arg = encoded(null)")
    {
      const array_string_exprt arg = from_char_vector(
        encoded('N', {0, 0, 0, 0}, "null"),
        char_type,
        length_type,
        pointer_type,
        array_pool);
      format_specifiert fs{0, "", 0, 0, false, format_specifiert::CHARACTER};
      const exprt actual =
        length_for_format_specifier(fs, arg, length_type, array_pool).value();
      const exprt simpl_actual = simplify_expr(actual, ns);
      THEN("length constraint represents 4")
      {
        const int to_int = *numeric_cast<int>(to_constant_expr(simpl_actual));
        REQUIRE(to_int == 4);
      }
    }
  }

  WHEN("format specifier = %b")
  {
    WHEN("arg = encoded(false)")
    {
      const array_string_exprt arg = from_char_vector(
        encoded('B', {0, 0, 0, 0}, "false"),
        char_type,
        length_type,
        pointer_type,
        array_pool);
      format_specifiert fs{0, "", 0, 0, false, format_specifiert::BOOLEAN};
      const exprt actual =
        length_for_format_specifier(fs, arg, length_type, array_pool).value();
      const exprt simpl_actual = simplify_expr(actual, ns);
      THEN("length constraint represents 5")
      {
        const int to_int = *numeric_cast<int>(to_constant_expr(simpl_actual));
        REQUIRE(to_int == 5);
      }
    }

    WHEN("arg = encoded(true)")
    {
      const array_string_exprt arg = from_char_vector(
        encoded('B', {0, 0, 0, 1}, "true"),
        char_type,
        length_type,
        pointer_type,
        array_pool);
      format_specifiert fs{0, "", 0, 0, false, format_specifiert::BOOLEAN};
      const exprt actual =
        length_for_format_specifier(fs, arg, length_type, array_pool).value();
      const exprt simpl_actual = simplify_expr(actual, ns);
      THEN("length constraint represents 4")
      {
        const int to_int = *numeric_cast<int>(to_constant_expr(simpl_actual));
        REQUIRE(to_int == 4);
      }
    }

    WHEN("arg = encoded(null)")
    {
      const array_string_exprt arg = from_char_vector(
        encoded('N', {0, 0, 0, 0}, "null"),
        char_type,
        length_type,
        pointer_type,
        array_pool);
      format_specifiert fs{0, "", 0, 0, false, format_specifiert::BOOLEAN};
      const exprt actual =
        length_for_format_specifier(fs, arg, length_type, array_pool).value();
      const exprt simpl_actual = simplify_expr(actual, ns);
      THEN(
        "length constraint represents 5 (because it should 'false', not 'null'")
      {
        const int to_int = *numeric_cast<int>(to_constant_expr(simpl_actual));
        REQUIRE(to_int == 5);
      }
    }
  }
}
