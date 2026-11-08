/*******************************************************************\

Module: String solver

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// Lengths of the arrays that the array pool makes for pointers.

#include "string_length_ties.h"

#include <util/invariant.h>
#include <util/std_expr.h>

#include "array_pool.h"

#include <functional>
#include <set>
#include <unordered_set>

/// The number of merges that canonical() follows; each is a join point of
/// the program, so this bounds the number of loop unwindings it sees through.
static const std::size_t max_merges = 64;

static bool is_integer_type(const typet &type)
{
  return type.id() == ID_signedbv || type.id() == ID_unsignedbv ||
         type.id() == ID_integer;
}

void string_length_tiest::add_definitions(const std::vector<exprt> &equations)
{
  for(const auto &equation : equations)
  {
    const auto equal = expr_try_dynamic_cast<equal_exprt>(equation);
    if(
      !equal || equal->lhs().id() != ID_symbol ||
      !is_integer_type(equal->lhs().type()) ||
      equal->lhs().type() != equal->rhs().type())
    {
      continue;
    }
    const exprt &rhs = equal->rhs();
    if(
      rhs.id() == ID_symbol || rhs.id() == ID_member ||
      rhs.id() == ID_nondet_symbol)
    {
      copies.make_union(equal->lhs(), rhs);
    }
    else if(rhs.id() == ID_if)
      merges.emplace(equal->lhs(), rhs);
  }
}

exprt string_length_tiest::canonical(exprt length) const
{
  for(std::size_t step = 0; step < max_merges; ++step)
  {
    copies.replace_expr(length);
    const auto merge = merges.find(length);
    if(merge == merges.end())
      break;
    length = merge->second;
  }
  copies.replace_expr(length);
  return length;
}

std::vector<exprt> string_length_tiest::new_ties(const array_poolt &pool)
{
  // The canonical lengths of each pointer that is not an if-expression. A
  // find of c ? p1 : p2 with length c ? l1 : l2 is a find of p1 with l1 and
  // of p2 with l2. With any other length l, it is c ? (p1, l) : (p2, l),
  // so it is a find of p1 and of p2 with l.
  std::unordered_map<exprt, std::unordered_set<exprt, irep_hash>, irep_hash>
    lengths;
  // Each pair of nodes once: the if-expressions of strings merged at join
  // points of a program share their subexpressions.
  // The lengths are kept, so that their nodes are not reused.
  std::set<std::pair<const void *, const void *>> visited;
  std::vector<exprt> visited_lengths;
  std::function<void(const exprt &, const exprt &)> record =
    [&](const exprt &pointer, const exprt &length)
  {
    if(!visited.emplace(&pointer.read(), &length.read()).second)
      return;
    visited_lengths.push_back(length);
    const auto pointer_if = expr_try_dynamic_cast<if_exprt>(pointer);
    if(!pointer_if)
    {
      lengths[pointer].insert(length);
      return;
    }
    const auto length_if = expr_try_dynamic_cast<if_exprt>(length);
    if(length_if && length_if->cond() == pointer_if->cond())
    {
      record(pointer_if->true_case(), canonical(length_if->true_case()));
      record(pointer_if->false_case(), canonical(length_if->false_case()));
    }
    else
    {
      record(pointer_if->true_case(), length);
      record(pointer_if->false_case(), length);
    }
  };
  for(const auto &entry : pool.found_lengths())
  {
    for(const auto &length : entry.second)
      record(entry.first, canonical(length));
  }

  std::vector<exprt> ties;
  for(const auto &made : pool.arrays_made_for_pointers())
  {
    const exprt &pointer = made.first;
    const auto found = lengths.find(pointer);
    const auto previous = tied.find(pointer);
    if(previous != tied.end())
    {
      // A later find may only repeat the length the pointer was tied to.
      if(
        found != lengths.end() && (found->second.size() != 1 ||
                                   *found->second.begin() != previous->second))
      {
        has_conflict = true;
      }
      continue;
    }
    if(found == lengths.end() || found->second.size() != 1)
      continue;
    const exprt &length = *found->second.begin();
    const auto pool_length_opt = pool.get_length_if_exists(made.second);
    INVARIANT(
      pool_length_opt.has_value(),
      "arrays made for pointers have a length in the pool");
    const exprt &pool_length = *pool_length_opt;
    ties.push_back(equal_exprt{
      pool_length,
      typecast_exprt::conditional_cast(length, pool_length.type())});
    tied.emplace(pointer, length);
  }
  return ties;
}
