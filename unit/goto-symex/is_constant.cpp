/*******************************************************************\

Module: Unit tests for goto_symex_can_forward_propagatet

Author: Diffblue Ltd.

\*******************************************************************/

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/namespace.h>
#include <util/std_expr.h>
#include <util/symbol_table.h>

#include <goto-symex/goto_symex_can_forward_propagate.h>
#include <testing-utils/use_catch.h>

SCENARIO("goto-symex-is-constant", "[core][goto-symex][is_constant]")
{
  symbol_tablet symbol_table;
  namespacet ns{symbol_table};

  signedbv_typet int_type(32);
  constant_exprt sizeof_constant("4", int_type);
  sizeof_constant.set(ID_C_c_sizeof_type, int_type);
  symbol_exprt non_constant("x", int_type);

  goto_symex_can_forward_propagatet is_constant(ns);

  GIVEN("Sizeof expression multiplied by a non-constant")
  {
    mult_exprt non_const_by_sizeof(non_constant, sizeof_constant);
    mult_exprt sizeof_by_non_const(sizeof_constant, non_constant);
    WHEN("We check whether goto-symex regards the expression as constant")
    {
      bool result1 = is_constant(non_const_by_sizeof);
      bool result2 = is_constant(sizeof_by_non_const);

      THEN("Should be constant")
      {
        REQUIRE(result1);
        REQUIRE(result2);
      }
    }
  }

  GIVEN("Updates of a constant array")
  {
    const array_typet array_type{int_type, from_integer(4096, size_type())};
    const array_of_exprt zeros{from_integer(0, int_type), array_type};
    const with_exprt constant_update{
      with_exprt{
        zeros, from_integer(0, size_type()), from_integer(7, int_type)},
      from_integer(1, size_type()),
      from_integer(8, int_type)};
    const with_exprt symbolic_value{
      zeros, from_integer(0, size_type()), non_constant};
    const with_exprt symbolic_index{
      zeros,
      typecast_exprt{non_constant, size_type()},
      from_integer(7, int_type)};
    exprt long_chain = zeros;
    for(int i = 0; i < 65; ++i)
    {
      long_chain = with_exprt{
        long_chain, from_integer(i, size_type()), from_integer(i, int_type)};
    }
    WHEN("We check whether goto-symex regards them as constant")
    {
      THEN("Constant values at constant indices are")
      {
        REQUIRE(is_constant(constant_update));
      }
      THEN("Symbolic values or indices are not")
      {
        REQUIRE(!is_constant(symbolic_value));
        REQUIRE(!is_constant(symbolic_index));
      }
      THEN("Nor are more than 64 updates")
      {
        REQUIRE(!is_constant(long_chain));
      }
    }
  }

  GIVEN("Non-multiply expression involving a sizeof expression")
  {
    plus_exprt non_const_plus_sizeof(non_constant, sizeof_constant);
    WHEN("We check whether goto-symex regards the expression as constant")
    {
      bool result = is_constant(non_const_plus_sizeof);

      THEN("Should not be constant")
      {
        REQUIRE(!result);
      }
    }
  }
}
