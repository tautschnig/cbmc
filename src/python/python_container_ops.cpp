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
