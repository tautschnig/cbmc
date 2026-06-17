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
    exprt other = convert_expression(*as_array(args).begin());
    if(!is_python_set_type(other.type()))
      return side_effect_expr_nondett{python_set_type(), get_location(expr)};
    exprt rbm = arg_bitmap(other);
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
    // Return nondet int and clear a nondet bit — over-approximation
    // that reflects pop removing an arbitrary element.
    pending_checks.push_back(code_frontend_assignt{
      bm, side_effect_expr_nondett{unsignedbv_typet{64}, get_location(expr)}});
    return side_effect_expr_nondett{signedbv_typet{64}, get_location(expr)};
  }

  return std::nullopt;
}
