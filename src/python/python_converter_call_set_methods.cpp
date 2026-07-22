/// Python to GOTO converter — set-method dispatch
/// (PLR §6.4.6 set methods). Operates on the 64-bit bitmap
/// representation so it stays precise for int elements in
/// [0, 64). Falls through to a sound nondet for elements
/// outside that range or for set-typed structs whose
/// elements are not int. Extracted from
/// python_converter_call_method.cpp as a pure source-split
/// (documented in doc/python-frontend-architecture.md); kept
/// in its own file for clarity given the bitmap-specific logic.
/// Semantics are preserved.

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/json.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/std_types.h>
#include <util/symbol.h>

#include "python_converter.h"
#include "python_converter_helpers.h"
#include "python_types.h"

#include <cstdint>
#include <optional>
#include <string>

std::optional<exprt> python_convertert::try_set_method(
  const jsont &expr,
  const exprt &obj,
  const typet &obj_base_type,
  const std::string &method_name,
  const jsont &args)
{
  member_exprt bm{obj, "bitmap", unsignedbv_typet{64}};
  member_exprt off{obj, "offset", signedbv_typet{64}};

  auto arg_bitmap = [&](const exprt &s) {
    return member_exprt{s, "bitmap", unsignedbv_typet{64}};
  };

  // Read a single int arg; cast to unsigned{64} for shifts.
  auto single_arg_bit_shift = [&]() -> std::optional<exprt>
  {
    if(!args.is_array() || as_array(args).empty())
      return std::nullopt;
    exprt val = convert_expression(*as_array(args).begin());
    if(val.type() != signedbv_typet{64})
      val = safe_typecast(val, signedbv_typet{64});
    exprt shamt = typecast_exprt{val, unsignedbv_typet{64}};
    return shamt;
  };

  if(method_name == "add" || method_name == "discard")
  {
    if(!args.is_array() || as_array(args).empty())
      return side_effect_expr_nondett{bool_typet{}, get_location(expr)};
    exprt val = convert_expression(*as_array(args).begin());
    // PLR §3.2: set elements must be hashable; a list/dict/set argument to
    // add/discard raises TypeError ('unhashable type').
    if(!val.is_nil() && is_unhashable_type(val.type()))
    {
      emit_conditional_exception(true_exprt{}, "TypeError");
      return side_effect_expr_nondett{python_int_type(), get_location(expr)};
    }
    // The set is a 64-bit int BITMAP -- only int/bool elements have a precise
    // bit. A non-int element (tuple/str/...) cannot be represented: casting it
    // to a bit position can COLLIDE with another element's bit (false-proving
    // membership) AND a no-op would leave popcount unchanged (false-proving
    // len(), which is popcount(bitmap)). Sound over-approximation: havoc the
    // bitmap so both int membership and len() become nondet; non-int
    // membership is independently nondet at the `in` site (convert_compare).
    {
      const typet &vt = val.type();
      const bool int_elem = vt.id() == ID_signedbv ||
                            vt.id() == ID_unsignedbv || vt.id() == ID_bool ||
                            vt.id() == ID_integer;
      if(!int_elem)
      {
        pending_checks.push_back(code_frontend_assignt{
          bm,
          side_effect_expr_nondett{unsignedbv_typet{64}, get_location(expr)}});
        return side_effect_expr_nondett{python_int_type(), get_location(expr)};
      }
    }
    if(val.type() != signedbv_typet{64})
      val = safe_typecast(val, signedbv_typet{64});
    // `add` introduces an element: guard it lies in the modelled bitmap range,
    // else `1 << val` overflows and the element is silently dropped (unsound).
    // `discard` of an out-of-range element is a harmless no-op (it cannot be
    // present), so it needs no guard.
    if(method_name == "add")
      emit_set_range_guard(pending_checks, val, get_location(expr));
    exprt shamt = typecast_exprt{val, unsignedbv_typet{64}};
    exprt bit = shl_exprt{from_integer(1, unsignedbv_typet{64}), shamt};
    exprt new_bm = method_name == "add"
                     ? exprt{bitor_exprt{bm, bit}}
                     : exprt{bitand_exprt{bm, bitnot_exprt{bit}}};
    pending_checks.push_back(code_frontend_assignt{bm, new_bm});
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  if(method_name == "remove")
  {
    // Like discard, but raise KeyError if the element is
    // not present.
    auto shamt = single_arg_bit_shift();
    if(!shamt.has_value())
      return side_effect_expr_nondett{bool_typet{}, get_location(expr)};
    exprt bit = shl_exprt{from_integer(1, unsignedbv_typet{64}), *shamt};
    exprt present = notequal_exprt{
      bitand_exprt{bm, bit}, from_integer(0, unsignedbv_typet{64})};
    add_check(
      present, "exception", "KeyError: element not in set", get_location(expr));
    exprt new_bm = bitand_exprt{bm, bitnot_exprt{bit}};
    pending_checks.push_back(code_frontend_assignt{bm, new_bm});
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  if(
    method_name == "union" || method_name == "intersection" ||
    method_name == "difference" || method_name == "symmetric_difference")
  {
    if(!args.is_array() || as_array(args).empty())
      return obj;
    // PLR §6.10.2: these methods are VARIADIC (a.union(b, c, ...));
    // folding only the first argument dropped the rest
    // ({1}.union({2}, {3}) proved 3 absent -- a false proof). Fold the
    // arguments left-to-right; a non-set argument degrades the whole
    // result to nondet as before.
    exprt other = convert_expression(*as_array(args).begin());
    if(!is_python_set_type(other.type()))
      return side_effect_expr_nondett{python_set_type(), get_location(expr)};
    exprt rbm = arg_bitmap(other);
    for(auto ait = std::next(as_array(args).begin());
        ait != as_array(args).end();
        ++ait)
    {
      exprt more = convert_expression(*ait);
      if(!is_python_set_type(more.type()))
        return side_effect_expr_nondett{python_set_type(), get_location(expr)};
      exprt mbm = arg_bitmap(more);
      if(
        method_name == "union" || method_name == "update" ||
        method_name == "difference")
      {
        // union: a | b | c. difference: a - b - c == a & ~(b | c), so
        // the ARGUMENT bitmaps accumulate by OR in both cases (the
        // and-not is applied once below).
        rbm = bitor_exprt{std::move(rbm), std::move(mbm)};
      }
      else if(method_name == "intersection")
        rbm = bitand_exprt{std::move(rbm), std::move(mbm)};
      else
      {
        // symmetric_difference is unary in CPython (2+ args raise
        // TypeError); degrade to nondet.
        return side_effect_expr_nondett{python_set_type(), get_location(expr)};
      }
    }

    exprt new_bm;
    if(method_name == "union")
      new_bm = bitor_exprt{bm, rbm};
    else if(method_name == "intersection")
      new_bm = bitand_exprt{bm, rbm};
    else if(method_name == "difference")
      new_bm = bitand_exprt{bm, bitnot_exprt{rbm}};
    else // symmetric_difference
      new_bm = bitxor_exprt{bm, rbm};
    return struct_exprt{{std::move(new_bm), off}, python_set_type()};
  }
  if(
    method_name == "issubset" || method_name == "issuperset" ||
    method_name == "isdisjoint")
  {
    if(!args.is_array() || as_array(args).empty())
      return true_exprt{};
    exprt other = convert_expression(*as_array(args).begin());
    if(!is_python_set_type(other.type()))
      return side_effect_expr_nondett{bool_typet{}, get_location(expr)};
    exprt rbm = arg_bitmap(other);
    exprt zero = from_integer(0, unsignedbv_typet{64});
    if(method_name == "issubset")
      // A⊆B iff A & ~B == 0
      return equal_exprt{bitand_exprt{bm, bitnot_exprt{rbm}}, zero};
    if(method_name == "issuperset")
      return equal_exprt{bitand_exprt{rbm, bitnot_exprt{bm}}, zero};
    // isdisjoint: A & B == 0
    return equal_exprt{bitand_exprt{bm, rbm}, zero};
  }
  if(method_name == "copy")
    return obj;
  if(method_name == "clear")
  {
    pending_checks.push_back(
      code_frontend_assignt{bm, from_integer(0, unsignedbv_typet{64})});
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  if(method_name == "pop")
  {
    // pop() removes and returns an ARBITRARY element. Model the result as a
    // nondet index in [0, 64) whose bit is SET in the (pre-pop) bitmap, then
    // clear that bit. This is sound (the result is always an actual element,
    // and all elements remain reachable) AND precise for singletons (a single
    // set bit pins the returned value). Empty set raises KeyError.
    emit_conditional_exception(
      equal_exprt{bm, from_integer(0, unsignedbv_typet{64})}, "KeyError");
    static unsigned set_pop_ctr = 0;
    const std::string nm = "__set_pop_" + std::to_string(set_pop_ctr++);
    const irep_idt rid{qualify_name(nm)};
    if(symbol_table.lookup(rid) == nullptr)
    {
      symbolt sy{rid, signedbv_typet{64}, "python"};
      sy.base_name = nm;
      sy.is_lvalue = true;
      sy.is_state_var = true;
      symbol_table.add(sy);
    }
    const symbol_exprt r = symbol_table.lookup_ref(rid).symbol_expr();
    pending_checks.push_back(code_frontend_assignt{
      r, side_effect_expr_nondett{signedbv_typet{64}, get_location(expr)}});
    // 0 <= r < 64
    pending_checks.push_back(code_assumet{and_exprt{
      binary_relation_exprt{r, ID_ge, from_integer(0, signedbv_typet{64})},
      binary_relation_exprt{r, ID_lt, from_integer(64, signedbv_typet{64})}}});
    // bit = 1u << r ; assume the bit is set (r is a member)
    const exprt bit = shl_exprt{
      from_integer(1, unsignedbv_typet{64}),
      typecast_exprt{r, unsignedbv_typet{64}}};
    // Assume the chosen bit is a member -- BUT permit the empty-set case
    // (bm == 0): otherwise this assume is `0 != 0` (false) on the empty path,
    // which would silently CUT it and mask the KeyError emitted above (a false
    // proof: `set().pop()` would vacuously verify). With the bm==0 disjunct the
    // empty path stays feasible so the KeyError uncaught-exception assert fires.
    pending_checks.push_back(code_assumet{or_exprt{
      equal_exprt{bm, from_integer(0, unsignedbv_typet{64})},
      notequal_exprt{
        bitand_exprt{bm, bit}, from_integer(0, unsignedbv_typet{64})}}});
    // clear the popped bit
    pending_checks.push_back(
      code_frontend_assignt{bm, bitand_exprt{bm, bitnot_exprt{bit}}});
    return std::move(r);
  }

  return std::nullopt;
}
