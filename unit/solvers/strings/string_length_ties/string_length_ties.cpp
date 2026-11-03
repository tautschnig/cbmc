/*******************************************************************\

Module: Unit tests for string_length_tiest

Author: Michael Tautschnig

\*******************************************************************/

#include <util/pointer_expr.h>

#include <solvers/strings/array_pool.h>
#include <solvers/strings/string_length_ties.h>
#include <testing-utils/use_catch.h>

SCENARIO("string_length_ties", "[core][solvers][strings][string_length_ties]")
{
  const auto char_type = unsignedbv_typet{8};
  const auto length_type = signedbv_typet{32};
  const auto pointer_type = pointer_typet{char_type, 16};
  const symbol_exprt p1{"p1", pointer_type};
  const symbol_exprt p2{"p2", pointer_type};
  const symbol_exprt l1{"l1", length_type};
  const symbol_exprt l2{"l2", length_type};
  const symbol_exprt l3{"l3", length_type};
  const symbol_exprt c{"c", bool_typet{}};

  symbol_generatort symbol_generator;
  array_poolt pool{symbol_generator};
  string_length_tiest ties;

  // The length that \p tie sets, if it is length(array of pointer) == l.
  const auto tied_length =
    [&](const std::vector<exprt> &all, const exprt &pointer) -> exprt
  {
    const array_string_exprt array = pool.find(pointer, l1);
    const exprt pool_length = pool.get_or_create_length(array);
    for(const auto &tie : all)
    {
      const auto &equal = to_equal_expr(tie);
      if(equal.lhs() == pool_length)
        return equal.rhs();
    }
    return nil_exprt{};
  };

  GIVEN("A pointer found with one length")
  {
    pool.find(p1, l1);
    pool.find(p1, l1);
    THEN("Its length is tied to that length")
    {
      const auto all = ties.new_ties(pool);
      REQUIRE(all.size() == 1);
      REQUIRE(tied_length(all, p1) == l1);
      REQUIRE_FALSE(ties.conflict());
    }
  }

  GIVEN("A pointer found with two lengths that may differ")
  {
    pool.find(p1, l1);
    pool.find(p1, l2);
    THEN("Its length is not tied")
    {
      REQUIRE(ties.new_ties(pool).empty());
    }
  }

  GIVEN("A pointer found with a length and with a copy of it")
  {
    ties.add_definitions({equal_exprt{l2, l1}});
    pool.find(p1, l1);
    pool.find(p1, l2);
    THEN("Its length is tied")
    {
      REQUIRE(ties.new_ties(pool).size() == 1);
    }
  }

  GIVEN("A merged pointer found with a length merged on the same condition")
  {
    ties.add_definitions({equal_exprt{l3, if_exprt{c, l1, l2}}});
    pool.find(if_exprt{c, p1, p2}, l3);
    THEN("Each branch is tied to its own length")
    {
      const auto all = ties.new_ties(pool);
      REQUIRE(all.size() == 2);
      REQUIRE(tied_length(all, p1) == l1);
      REQUIRE(tied_length(all, p2) == l2);
    }
  }

  GIVEN("A merged pointer found with a single length")
  {
    pool.find(if_exprt{c, p1, p2}, l1);
    THEN("Both branches are tied to that length")
    {
      const auto all = ties.new_ties(pool);
      REQUIRE(all.size() == 2);
      REQUIRE(tied_length(all, p1) == l1);
      REQUIRE(tied_length(all, p2) == l1);
    }
  }

  GIVEN("A merged pointer whose length is merged on another condition")
  {
    const symbol_exprt d{"d", bool_typet{}};
    ties.add_definitions({equal_exprt{l3, if_exprt{d, l1, l2}}});
    pool.find(if_exprt{c, p1, p2}, l3);
    pool.find(p1, l1);
    THEN("A branch also found with another length is not tied")
    {
      const auto all = ties.new_ties(pool);
      REQUIRE(all.size() == 1);
      REQUIRE(tied_length(all, p2) == if_exprt{d, l1, l2});
    }
  }

  GIVEN("A tied pointer that is later found with another length")
  {
    pool.find(p1, l1);
    REQUIRE(ties.new_ties(pool).size() == 1);
    pool.find(p1, l2);
    THEN("The ties report a conflict")
    {
      REQUIRE(ties.new_ties(pool).empty());
      REQUIRE(ties.conflict());
    }
  }
}
