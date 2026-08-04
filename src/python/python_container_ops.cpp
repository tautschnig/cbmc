/// \file
/// Python to GOTO converter — container-operation choke points.
///
/// P0 of doc/python-frontend-unbounded-containers-plan.md: the
/// bounded lowerings of container operations (dict key lookup,
/// element equality, boxed-dict access) are consolidated here so
/// every call site shares ONE spelling of each step. Before this
/// file, the per-slot key/element comparison was open-coded at
/// ten+ sites and had diverged: the read-side copies (get,
/// subscript read, In/NotIn) compared string keys by CONTENT via
/// the string solver, while the write/mutate-side copies
/// (setdefault, pop, del, subscript write, list index/count/
/// remove) still used raw struct equality — the refined-string
/// DATA POINTER — and falsely refuted any needle built at
/// runtime (loop-concatenated keys, values that crossed a call
/// boundary). PLR §6.4.6 / §6.10.1: dict lookup and sequence
/// membership compare by VALUE.
///
/// P1 re-targets these helpers to cprover_list_*/cprover_dict_*
/// function applications under --python-smt-containers; the
/// call-site contracts stay unchanged.

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/expr.h>
#include <util/floatbv_expr.h>
#include <util/pointer_expr.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/std_types.h>

#include "python_converter.h"
#include "python_converter_helpers.h"
#include "python_types.h"
#include "python_value_type.h"

exprt python_convertert::container_slot_equal(
  const exprt &a,
  const exprt &b,
  std::vector<codet> *sink)
{
  // Tagged unions: dispatch on the runtime tag (PLR §3.3). If only
  // one side is boxed, box the other — value_equal requires two
  // python_value operands and compares STR content, FLOAT/BOOL/INT
  // payloads under a tag guard.
  const bool a_pv = is_python_value_type(a.type());
  const bool b_pv = is_python_value_type(b.type());
  if(a_pv || b_pv)
    return value_equal(a_pv ? a : wrap_value(a), b_pv ? b : wrap_value(b));

  // Strings: CONTENT equality via the string solver. Raw struct
  // equality compares the data pointer of the refined-string view
  // (or the raw handle bits on the native backend) and wrongly
  // refutes equal content with distinct storage.
  if(is_python_string_type(a.type()) && is_python_string_type(b.type()))
  {
    exprt eq = emit_string_bool_function(
      ID_cprover_string_equal_func,
      a,
      b,
      symbol_table,
      sink != nullptr ? *sink : pending_checks);
    if(eq.type() != bool_typet{})
      eq = typecast_exprt{std::move(eq), bool_typet{}};
    return eq;
  }

  // Scalars / identical layouts: plain equality, with a
  // value-preserving cast when the scalar types differ
  // (PLR §6.10.1: 1 == 1.0 is True).
  exprt rhs = b;
  if(a.type() != rhs.type())
    rhs = safe_typecast(rhs, a.type());
  if(a.type().id() == ID_floatbv)
    return ieee_float_equal_exprt{a, rhs};
  return equal_exprt{a, rhs};
}

exprt python_convertert::dict_slot_match(
  const exprt &keys_array,
  const exprt &length,
  std::size_t i,
  const exprt &key_probe,
  std::vector<codet> *sink)
{
  const exprt idx = from_integer(i, signedbv_typet{64});
  const exprt in_range = binary_relation_exprt{idx, ID_lt, length};
  exprt key_at = python_dict_unbox_key(index_exprt{keys_array, idx});
  exprt probe = python_dict_unbox_key(key_probe);
  return and_exprt{in_range, container_slot_equal(key_at, probe, sink)};
}

typet python_convertert::canonical_str_dict_type() const
{
  return python_dict_type(python_string_type(), python_value_type());
}

dereference_exprt python_convertert::boxed_dict_deref(const exprt &boxed) const
{
  return dereference_exprt{typecast_exprt{
    python_value_class_ptr(boxed),
    pointer_typet{canonical_str_dict_type(), 64}}};
}

exprt python_convertert::build_list_data(
  exprt::operandst elements,
  const array_typet &data_array) const
{
  const typet &slot_type = data_array.element_type();
  if(python_smt_containers_flag())
  {
    exprt data = array_of_exprt{safe_zero(slot_type), data_array};
    for(std::size_t i = 0; i < elements.size(); i++)
    {
      data = with_exprt{
        std::move(data),
        from_integer(i, signedbv_typet{64}),
        std::move(elements[i])};
    }
    return data;
  }
  // Pad/trim to the array type's OWN declared size (a literal larger
  // than PYTHON_MAX_LIST_LENGTH gets a grown array type — e.g.
  // stdlib __all__ lists; see convert_list).
  std::size_t cap = PYTHON_MAX_LIST_LENGTH;
  mp_integer size_val;
  if(
    data_array.size().is_constant() &&
    !to_integer(to_constant_expr(data_array.size()), size_val))
    cap = static_cast<std::size_t>(size_val.to_ulong());
  while(elements.size() < cap)
    elements.push_back(safe_zero(slot_type));
  if(elements.size() > cap)
    elements.resize(cap);
  return array_exprt{std::move(elements), data_array};
}

exprt python_convertert::build_list_data(
  exprt::operandst elements,
  const typet &element_type) const
{
  const typet list_type = python_list_type(element_type);
  // python_list_type may rewrite the element type (str -> handle on
  // the native string backend); honor the SLOT type it chose.
  const auto &data_array =
    to_array_type(to_struct_type(list_type).components()[1].type());
  const typet &slot_type = data_array.element_type();

  (void)slot_type;
  return build_list_data(std::move(elements), data_array);
}

exprt python_convertert::build_list_value(
  exprt::operandst elements,
  const typet &element_type) const
{
  const std::size_t n = elements.size();
  const typet list_type = python_list_type(element_type);
  exprt data = build_list_data(std::move(elements), element_type);
  return struct_exprt{
    {from_integer(n, signedbv_typet{64}), std::move(data)}, list_type};
}

std::optional<exprt>
python_convertert::list_literal_element(const exprt &data, std::size_t i) const
{
  if(data.id() == ID_array)
  {
    if(i < data.operands().size())
      return data.operands()[i];
    return {};
  }
  // Store chain: newest store wins — walk outside-in.
  const exprt *e = &data;
  while(e->id() == ID_with)
  {
    const auto &w = to_with_expr(*e);
    // with_exprt supports multi-update (where/value pairs); ours are
    // built pairwise, but decode the general form.
    for(std::size_t k = 1; k + 1 < e->operands().size(); k += 2)
    {
      const exprt &where = e->operands()[k];
      mp_integer idx_val;
      if(where.is_constant() && !to_integer(to_constant_expr(where), idx_val))
      {
        if(idx_val == static_cast<long long>(i))
          return e->operands()[k + 1];
      }
      else
        return {}; // symbolic store index — cannot decode statically
    }
    e = &w.old();
  }
  if(e->id() == ID_array_of)
    return to_array_of_expr(*e).what();
  return {};
}

std::optional<std::size_t>
python_convertert::list_literal_data_size(const exprt &data) const
{
  if(data.id() == ID_array)
    return data.operands().size();
  std::size_t max_idx = 0;
  bool any = false;
  const exprt *e = &data;
  while(e->id() == ID_with)
  {
    const auto &w = to_with_expr(*e);
    for(std::size_t k = 1; k + 1 < e->operands().size(); k += 2)
    {
      const exprt &where = e->operands()[k];
      mp_integer idx_val;
      if(!where.is_constant() || to_integer(to_constant_expr(where), idx_val))
        return {};
      const std::size_t iv = static_cast<std::size_t>(idx_val.to_ulong());
      max_idx = std::max(max_idx, iv + 1);
      any = true;
    }
    e = &w.old();
  }
  if(e->id() == ID_array_of)
    return any ? std::optional<std::size_t>{max_idx}
               : std::optional<std::size_t>{0};
  return {};
}

void python_convertert::emit_scan_bound_guard(
  const exprt &length,
  const source_locationt &loc)
{
  if(!python_smt_containers_flag())
    return;
  binary_relation_exprt in_bounds{
    length, ID_le, from_integer(PYTHON_MAX_LIST_LENGTH, length.type())};
  source_locationt aloc = loc;
  aloc.set_property_class("python-model-bound");
  aloc.set_comment(
    "operation scans a bounded prefix (verifier model bound; "
    "--python-smt-containers P1 covers index/append/len/slice)");
  code_assertt bound_assert{in_bounds};
  bound_assert.add_source_location() = aloc;
  pending_checks.push_back(std::move(bound_assert));
  code_assumet bound_assume{in_bounds};
  bound_assume.add_source_location() = loc;
  pending_checks.push_back(std::move(bound_assume));
}

std::optional<exprt::operandst>
python_convertert::list_literal_leading(const exprt &list_value) const
{
  if(list_value.id() != ID_struct || list_value.operands().size() < 2)
    return {};
  const exprt &len = list_value.operands()[0];
  mp_integer len_val;
  if(!len.is_constant() || to_integer(to_constant_expr(len), len_val))
    return {};
  const exprt &data = list_value.operands()[1];
  exprt::operandst out;
  const std::size_t n = static_cast<std::size_t>(len_val.to_ulong());
  for(std::size_t i = 0; i < n; i++)
  {
    auto el = list_literal_element(data, i);
    if(!el.has_value())
      return {};
    out.push_back(std::move(*el));
  }
  return out;
}
