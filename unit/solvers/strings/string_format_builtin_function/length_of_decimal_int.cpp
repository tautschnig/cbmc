/*******************************************************************\

Module: Unit tests for length_of_decimal_int in
        solvers/refinement/string_format_builtin_function.cpp

Author: Diffblue Ltd.

\*******************************************************************/

#include <util/simplify_expr.h>

#include <solvers/strings/string_format_builtin_function.h>
#include <testing-utils/empty_namespace.h>
#include <testing-utils/use_catch.h>

SCENARIO(
  "length_of_decimal_int",
  "[core][solvers][strings][string_format_builtin_function]")
{
  const typet type = signedbv_typet(32);

  const std::vector<mp_integer> input_values = {
    0, 1, 10, 15, 999, 1000000001, -1, -21111111, -1234567890};
  const std::vector<int> oracles_for_base_10 = {1, 1, 2, 2, 3, 10, 2, 9, 11};
  const std::vector<int> oracles_for_base_16 = {1, 1, 1, 1, 3, 8, 2, 8, 9};

  WHEN("base = 10")
  {
    for(size_t i = 0; i < input_values.size(); i++)
    {
      exprt input_expr = from_integer(input_values[i], type);
      WHEN("input exprt = " << input_values[i])
      {
        const exprt actual = length_of_decimal_int(input_expr, type, 10);
        const int oracle = oracles_for_base_10[i];
        THEN("length expression is " << oracle)
        {
          const int actual_int =
            numeric_cast<int>(
              to_constant_expr(simplify_expr(actual, empty_namespace)))
              .value();
          REQUIRE(actual_int == oracle);
        }
      }
    }
  }

  WHEN("base = 16")
  {
    for(size_t i = 0; i < input_values.size(); i++)
    {
      exprt input_expr = from_integer(input_values[i], type);
      WHEN("input exprt = " << input_values[i])
      {
        const exprt actual = length_of_decimal_int(input_expr, type, 16);
        const int oracle = oracles_for_base_16[i];
        THEN("length expression is " << oracle)
        {
          const int actual_int = *numeric_cast<int>(
            to_constant_expr(simplify_expr(actual, empty_namespace)));
          REQUIRE(actual_int == oracle);
        }
      }
    }
  }
  WHEN("the integer has 64 bits")
  {
    const typet type64 = signedbv_typet(64);
    const std::vector<std::pair<mp_integer, int>> cases = {
      {12345678901, 11},
      {power(2, 63) - 1, 19},
      {-power(2, 63), 20},
      {-1000000000000, 14}};
    for(const auto &c : cases)
    {
      const exprt actual =
        length_of_decimal_int(from_integer(c.first, type64), type, 10);
      THEN("the length of " << c.first << " is " << c.second)
      {
        const int actual_int = *numeric_cast<int>(
          to_constant_expr(simplify_expr(actual, empty_namespace)));
        REQUIRE(actual_int == c.second);
      }
    }
  }
}
