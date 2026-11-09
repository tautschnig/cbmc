/*******************************************************************\

Module: Unit tests for array pool in
        solvers/refinement/string_constraint_generator.h

Author: Diffblue Ltd.

\*******************************************************************/

#include <testing-utils/use_catch.h>

#include <solvers/strings/array_pool.h>

#include <util/pointer_expr.h>

SCENARIO("array_pool", "[core][solvers][strings][string_constraint_generator]")
{
  const std::size_t pointer_width = 16;
  const auto char_type = unsignedbv_typet(8);
  const auto length_type = signedbv_typet(32);
  const auto pointer_type = pointer_typet(char_type, pointer_width);

  GIVEN("An array pool")
  {
    symbol_generatort symbol_generator;
    array_poolt pool(symbol_generator);

    WHEN("Looking for a pointer symbol")
    {
      const symbol_exprt pointer("my_pointer", pointer_type);
      const symbol_exprt pointer_length("my_pointer_length", length_type);
      const array_string_exprt associated_array =
        pool.find(pointer, pointer_length);

      THEN("A second find give the same array")
      {
        const array_string_exprt second_lookup =
          pool.find(pointer, pointer_length);

        REQUIRE(second_lookup == associated_array);
      }

      THEN("The array is unbounded, as its length is in the pool")
      {
        REQUIRE(
          to_array_type(associated_array.type()).size() ==
          infinity_exprt{length_type});
      }
    }

    WHEN("Associating a pointer with two arrays")
    {
      const symbol_exprt pointer("my_pointer", pointer_type);
      const array_typet array_type(char_type, infinity_exprt{length_type});
      const array_string_exprt first =
        to_array_string_expr(symbol_exprt("first_array", array_type));
      const array_string_exprt second =
        to_array_string_expr(symbol_exprt("second_array", array_type));
      pool.insert(pointer, first);

      THEN("The same array again is no conflict")
      {
        pool.insert(pointer, first);
        REQUIRE_FALSE(pool.has_conflicting_association());
      }

      THEN("Another array is recorded as a conflict")
      {
        pool.insert(pointer, second);
        REQUIRE(pool.has_conflicting_association());
      }
    }

    WHEN("Looking for a pointer symbol with a constant length")
    {
      const symbol_exprt pointer("my_pointer", pointer_type);
      const exprt length = from_integer(3, length_type);
      const array_string_exprt associated_array = pool.find(pointer, length);

      THEN("The array has that size")
      {
        REQUIRE(to_array_type(associated_array.type()).size() == length);
      }
    }

    WHEN("Looking for the address of the first element of a constant array")
    {
      const array_typet array_type(char_type, from_integer(3, length_type));
      const array_exprt array(
        {from_integer('f', char_type),
         from_integer('o', char_type),
         from_integer('o', char_type)},
        array_type);
      const exprt first_element =
        index_exprt(array, from_integer(0, length_type));
      const exprt pointer = address_of_exprt(first_element, pointer_type);
      const array_string_exprt associated_array =
        pool.find(pointer, array_type.size());

      THEN("The associated array should be the pointed array")
      {
        REQUIRE(associated_array == array);
      }
    }

    WHEN("Looking for a null pointer")
    {
      const null_pointer_exprt null_pointer(pointer_type);
      const symbol_exprt pointer_length("null_pointer_length", length_type);
      const array_string_exprt associated_array =
        pool.find(null_pointer, pointer_length);

      THEN("`find` always gives the same array")
      {
        const symbol_exprt pointer_length2("null_pointer_length2", length_type);
        const array_string_exprt second_lookup =
          pool.find(null_pointer, pointer_length2);

        REQUIRE(second_lookup == associated_array);
      }
    }

    WHEN("Looking for an if-expression")
    {
      const exprt boolean_symbol = symbol_exprt("boolean", bool_typet());
      const exprt true_case = symbol_exprt("pointer1", pointer_type);
      const exprt false_case = symbol_exprt("pointer2", pointer_type);
      const exprt if_expr = if_exprt(boolean_symbol, true_case, false_case);
      const symbol_exprt pointer_length("pointer_length", length_type);

      const array_string_exprt associated_array =
        pool.find(if_expr, pointer_length);

      THEN(
        "Arrays associated to the subexpressions are the subexpressions of "
        "the associated array")
      {
        const symbol_exprt pointer_length1("pointer_length1", length_type);
        const array_string_exprt associated_to_true =
          pool.find(true_case, pointer_length1);
        const symbol_exprt pointer_length2("pointer_length2", length_type);
        const array_string_exprt associated_to_false =
          pool.find(false_case, pointer_length2);

        // Arrays made for pointers with lengths that are not constants are
        // unbounded.
        const exprt unbounded = infinity_exprt{length_type};
        const typet recomposed_type = array_typet{
          char_type, if_exprt{boolean_symbol, unbounded, unbounded}};
        const exprt recomposed_array = if_exprt(
          boolean_symbol,
          associated_to_true,
          associated_to_false,
          recomposed_type);

        REQUIRE(associated_array == recomposed_array);
      }
    }

    WHEN("Looking for two pointer symbols")
    {
      const exprt true_case = symbol_exprt("pointer1", pointer_type);
      const symbol_exprt pointer_length1("pointer_length1", length_type);
      const exprt false_case = symbol_exprt("pointer2", pointer_type);
      const symbol_exprt pointer_length2("pointer_length2", length_type);

      const array_string_exprt associated_to_true =
        pool.find(true_case, pointer_length1);
      const array_string_exprt associated_to_false =
        pool.find(false_case, pointer_length2);

      THEN("Looking for an if-expression containing these two symbols")
      {
        const exprt boolean_symbol = symbol_exprt("boolean", bool_typet());
        const exprt if_expr = if_exprt(boolean_symbol, true_case, false_case);
        const symbol_exprt pointer_length("pointer_length", length_type);
        const array_string_exprt associated_array =
          pool.find(if_expr, pointer_length);

        // Arrays made for pointers with lengths that are not constants are
        // unbounded.
        const exprt unbounded = infinity_exprt{length_type};
        const typet recomposed_type = array_typet{
          char_type, if_exprt{boolean_symbol, unbounded, unbounded}};
        const exprt recomposed_array = if_exprt(
          boolean_symbol,
          associated_to_true,
          associated_to_false,
          recomposed_type);

        REQUIRE(associated_array == recomposed_array);
      }
    }
  }
}
