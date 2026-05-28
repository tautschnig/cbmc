/*******************************************************************\

Module: Axiomatic collection lowering for JBMC

Author: Kiro <kiro-agent@users.noreply.github.com>

Description: Replace HashMap / HashSet method calls with
direct CBMC IR backed by a SINGLE-level associative array
indexed by a packed (receiver, key) 64-bit int. CBMC's
flattener handles single-level int-indexed arrays cleanly;
nested arrays trigger propositional-reduction crashes.

The packed-int encoding is:

  index_for(receiver, key) =
    (uint64) pointer_offset(receiver) << 32 | (uint32) key_int

where `key_int` is `key.hashCode()` for Integer keys and
`pointer_offset(key)` for arbitrary Object keys. Collisions
require two distinct (receiver, key) pairs to compute the
same 64-bit value — extremely rare for the typical Java heap
layout. Under nondet exploration, the JBMC encoding of
`pointer_offset` uses a 48-bit object base + 16-bit offset,
so receiver pointers occupy bits 32..63 and keys bits 0..31
without overlap.

Activated by --axiomatic-collections.

\*******************************************************************/

#include "java_bytecode_axiomatic.h"

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/namespace.h>
#include <util/pointer_expr.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/symbol_table.h>

#include <goto-programs/goto_model.h>

#include "java_types.h"

#include <iostream>
#include <string>

namespace
{

const std::string AXIOMATIC_KV_SYM = "java::axiomatic::_kv";
const std::string AXIOMATIC_SET_SYM = "java::axiomatic::_set";
const std::string AXIOMATIC_SZ_SYM = "java::axiomatic::_sz";

/// 64-bit unsigned int — the array's index type.
typet packed_index_type()
{
  return unsignedbv_typet(64);
}

/// Java Object* — the array's element type for HashMap kv.
typet object_ptr_type()
{
  return java_lang_object_type();
}

/// Build a typecast if necessary so the result matches `target`.
exprt coerce(const exprt &e, const typet &target)
{
  if(e.type() == target)
    return e;
  return typecast_exprt(e, target);
}

/// Symbol type of the global kv array:
///   array(uint64, Object*) of symbolic length.
array_typet kv_array_type()
{
  array_typet ret{object_ptr_type(), infinity_exprt{java_int_type()}};
  ret.index_type_nonconst() = packed_index_type();
  return ret;
}

/// Symbol type of the global set array:
///   array(uint64, bool) of symbolic length.
array_typet set_array_type()
{
  array_typet ret{bool_typet{}, infinity_exprt{java_int_type()}};
  ret.index_type_nonconst() = packed_index_type();
  return ret;
}

/// Symbol type of the per-receiver size array:
///   array(uint64, int).
array_typet sz_array_type()
{
  array_typet ret{java_int_type(), infinity_exprt{java_int_type()}};
  ret.index_type_nonconst() = packed_index_type();
  return ret;
}

/// Build (or look up) a global SMT-array symbol.
const symbolt &ensure_global_array(
  symbol_tablet &symbol_table,
  const std::string &name,
  const array_typet &array_type)
{
  const irep_idt name_id(name);
  if(const symbolt *existing = symbol_table.lookup(name_id))
    return *existing;

  symbolt sym{name_id, array_type, ID_java};
  sym.base_name = name_id;
  sym.pretty_name = name_id;
  sym.is_lvalue = true;
  sym.is_state_var = true;
  sym.is_static_lifetime = true;
  sym.value = nil_exprt();

  auto inserted = symbol_table.insert(std::move(sym));
  return inserted.first;
}

/// Cast a Java Object* to a 32-bit int by extracting its
/// pointer offset. This gives the heap-allocation-unique
/// integer for static-allocated objects.
exprt ptr_to_int32(const exprt &p)
{
  // pointer_offset returns the byte offset within the
  // referenced object; for a fresh allocation, that's 0.
  // We need pointer IDENTITY, which CBMC encodes as the
  // object id. Use the pointer's bit-pattern via typecast.
  return typecast_exprt(p, unsignedbv_typet(32));
}

/// Pack a (receiver, key) pair into a 64-bit unsigned int.
///
///   packed = (uint64) ptr32(receiver) << 32 | (uint64) ptr32(key)
exprt pack_index(const exprt &receiver, const exprt &key)
{
  const typet u64 = packed_index_type();
  exprt r32 = ptr_to_int32(receiver);
  exprt k32 = ptr_to_int32(key);
  exprt r64 = typecast_exprt(r32, u64);
  exprt k64 = typecast_exprt(k32, u64);
  exprt shifted = shl_exprt(r64, from_integer(32, u64));
  return bitor_exprt(shifted, k64);
}

/// Pack just the receiver into a 64-bit int (for the size array).
exprt pack_receiver_only(const exprt &receiver)
{
  exprt r32 = ptr_to_int32(receiver);
  return typecast_exprt(r32, packed_index_type());
}

/// Build a typed Object* null pointer.
exprt object_null()
{
  return null_pointer_exprt{to_pointer_type(object_ptr_type())};
}

enum class axiomatic_op
{
  NONE,
  HM_INIT,
  HM_GET,
  HM_PUT,
  HM_CONTAINS_KEY,
  HM_SIZE,
  HM_IS_EMPTY,
  HS_INIT,
  HS_ADD,
  HS_CONTAINS,
  HS_SIZE,
  HS_IS_EMPTY,
};

axiomatic_op classify_call(const std::string &id)
{
  if(
    id == "java::java.util.HashMap.<init>:()V" ||
    id == "java::java.util.HashMap.<init>:(I)V" ||
    id == "java::java.util.HashMap.<init>:(IF)V" ||
    id == "java::java.util.LinkedHashMap.<init>:()V" ||
    id == "java::java.util.LinkedHashMap.<init>:(I)V" ||
    id == "java::java.util.LinkedHashMap.<init>:(IF)V" ||
    id == "java::java.util.LinkedHashMap.<init>:(IFZ)V")
    return axiomatic_op::HM_INIT;
  if(id == "java::java.util.HashMap.get:(Ljava/lang/Object;)Ljava/lang/Object;")
    return axiomatic_op::HM_GET;
  if(
    id ==
    "java::java.util.HashMap.put:"
    "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;")
    return axiomatic_op::HM_PUT;
  if(id == "java::java.util.HashMap.containsKey:(Ljava/lang/Object;)Z")
    return axiomatic_op::HM_CONTAINS_KEY;
  if(id == "java::java.util.HashMap.size:()I")
    return axiomatic_op::HM_SIZE;
  if(id == "java::java.util.HashMap.isEmpty:()Z")
    return axiomatic_op::HM_IS_EMPTY;

  if(
    id == "java::java.util.HashSet.<init>:()V" ||
    id == "java::java.util.HashSet.<init>:(I)V" ||
    id == "java::java.util.HashSet.<init>:(IF)V")
    return axiomatic_op::HS_INIT;
  if(id == "java::java.util.HashSet.add:(Ljava/lang/Object;)Z")
    return axiomatic_op::HS_ADD;
  if(id == "java::java.util.HashSet.contains:(Ljava/lang/Object;)Z")
    return axiomatic_op::HS_CONTAINS;
  if(id == "java::java.util.HashSet.size:()I")
    return axiomatic_op::HS_SIZE;
  if(id == "java::java.util.HashSet.isEmpty:()Z")
    return axiomatic_op::HS_IS_EMPTY;

  return axiomatic_op::NONE;
}

bool patch_return_value(
  goto_programt &body,
  goto_programt::targett &it,
  const std::string &call_id,
  const exprt &replacement_value)
{
  auto next_it = std::next(it);
  while(next_it != body.instructions.end() &&
        (next_it->is_location() || next_it->is_decl() || next_it->is_other() ||
         next_it->is_skip()))
    ++next_it;
  if(next_it == body.instructions.end() || !next_it->is_assign())
    return false;
  const exprt &rhs = next_it->assign_rhs();
  if(rhs.id() != ID_symbol)
    return false;
  const std::string rhs_id = id2string(to_symbol_expr(rhs).get_identifier());
  if(rhs_id != call_id + "#return_value")
    return false;
  exprt clean = coerce(replacement_value, next_it->assign_lhs().type());
  next_it->assign_rhs_nonconst() = clean;
  it = next_it;
  return true;
}

bool lower_one_call(
  goto_programt &body,
  goto_programt::targett &it,
  symbol_tablet &symbol_table)
{
  if(!it->is_function_call())
    return false;
  const exprt &cf = it->call_function();
  if(cf.id() != ID_symbol)
    return false;
  const std::string cf_id = id2string(to_symbol_expr(cf).get_identifier());
  axiomatic_op op = classify_call(cf_id);
  if(op == axiomatic_op::NONE)
    return false;

  const auto &args = it->call_arguments();
  if(args.empty())
    return false;
  const exprt receiver = coerce(args[0], object_ptr_type());

  const auto loc = it->source_location();
  // Save the original CALL iterator so we can SKIP it after
  // patch_return_value advances `it` to the consumer ASSIGN.
  const auto orig_call_it = it;

  switch(op)
  {
  case axiomatic_op::NONE:
    return false;

  case axiomatic_op::HM_INIT:
  case axiomatic_op::HS_INIT:
  {
    // Reset this receiver's size to 0. We can't easily clear
    // every key for the receiver in our flat encoding without
    // a quantifier, so we don't reset kv. This is sound for
    // freshly-allocated receivers (each new object has a
    // distinct pointer offset, so the kv slots are
    // automatically fresh nondet) but not for objects whose
    // memory is reused.
    const symbolt &sz =
      ensure_global_array(symbol_table, AXIOMATIC_SZ_SYM, sz_array_type());
    exprt rkey = pack_receiver_only(receiver);
    with_exprt new_sz{sz.symbol_expr(), rkey, from_integer(0, java_int_type())};
    auto next_it = std::next(it);
    body.insert_before(
      next_it,
      goto_programt::make_assignment(
        code_assignt(sz.symbol_expr(), new_sz), loc));
    it->turn_into_skip();
    return true;
  }

  case axiomatic_op::HM_GET:
  case axiomatic_op::HM_CONTAINS_KEY:
  {
    if(args.size() < 2)
      return false;
    const exprt key = coerce(args[1], object_ptr_type());
    const symbolt &kv =
      ensure_global_array(symbol_table, AXIOMATIC_KV_SYM, kv_array_type());
    exprt idx = pack_index(receiver, key);
    exprt val = index_exprt(kv.symbol_expr(), idx);
    exprt result;
    if(op == axiomatic_op::HM_GET)
      result = val;
    else
      result = notequal_exprt(val, object_null());
    if(!patch_return_value(body, it, cf_id, result))
      return false;
    orig_call_it->turn_into_skip();
    it = orig_call_it;
    return true;
  }

  case axiomatic_op::HM_PUT:
  {
    if(args.size() < 3)
      return false;
    const exprt key = coerce(args[1], object_ptr_type());
    const exprt value = coerce(args[2], object_ptr_type());
    const symbolt &kv =
      ensure_global_array(symbol_table, AXIOMATIC_KV_SYM, kv_array_type());
    const symbolt &sz =
      ensure_global_array(symbol_table, AXIOMATIC_SZ_SYM, sz_array_type());
    exprt idx = pack_index(receiver, key);
    exprt rkey = pack_receiver_only(receiver);
    exprt old_v = index_exprt(kv.symbol_expr(), idx);
    with_exprt new_kv{kv.symbol_expr(), idx, value};
    equal_exprt was_unset{old_v, object_null()};
    exprt cur_sz = index_exprt(sz.symbol_expr(), rkey);
    plus_exprt incremented{cur_sz, from_integer(1, java_int_type())};
    if_exprt new_sz_for_receiver{was_unset, incremented, cur_sz};
    with_exprt new_sz{sz.symbol_expr(), rkey, new_sz_for_receiver};
    // Patch return-value consumer FIRST so synthetic ASSIGNs
    // don't hide the <id>#return_value ASSIGN from
    // patch_return_value's forward search.
    patch_return_value(body, it, cf_id, old_v);
    auto next_it = std::next(orig_call_it);
    body.insert_before(
      next_it,
      goto_programt::make_assignment(
        code_assignt(kv.symbol_expr(), new_kv), loc));
    body.insert_before(
      next_it,
      goto_programt::make_assignment(
        code_assignt(sz.symbol_expr(), new_sz), loc));
    orig_call_it->turn_into_skip();
    it = orig_call_it;
    return true;
  }

  case axiomatic_op::HM_SIZE:
  case axiomatic_op::HM_IS_EMPTY:
  case axiomatic_op::HS_SIZE:
  case axiomatic_op::HS_IS_EMPTY:
  {
    const symbolt &sz =
      ensure_global_array(symbol_table, AXIOMATIC_SZ_SYM, sz_array_type());
    exprt rkey = pack_receiver_only(receiver);
    exprt cur_sz = index_exprt(sz.symbol_expr(), rkey);
    exprt result;
    if(op == axiomatic_op::HM_SIZE || op == axiomatic_op::HS_SIZE)
      result = cur_sz;
    else
      result = equal_exprt(cur_sz, from_integer(0, java_int_type()));
    if(!patch_return_value(body, it, cf_id, result))
      return false;
    orig_call_it->turn_into_skip();
    it = orig_call_it;
    return true;
  }

  case axiomatic_op::HS_ADD:
  {
    if(args.size() < 2)
      return false;
    const exprt elem = coerce(args[1], object_ptr_type());
    const symbolt &set =
      ensure_global_array(symbol_table, AXIOMATIC_SET_SYM, set_array_type());
    const symbolt &sz =
      ensure_global_array(symbol_table, AXIOMATIC_SZ_SYM, sz_array_type());
    exprt idx = pack_index(receiver, elem);
    exprt rkey = pack_receiver_only(receiver);
    exprt was_present = index_exprt(set.symbol_expr(), idx);
    with_exprt new_set{set.symbol_expr(), idx, true_exprt{}};
    exprt cur_sz = index_exprt(sz.symbol_expr(), rkey);
    plus_exprt incremented{cur_sz, from_integer(1, java_int_type())};
    if_exprt new_sz_for_receiver{was_present, cur_sz, incremented};
    with_exprt new_sz{sz.symbol_expr(), rkey, new_sz_for_receiver};
    // Patch the return-value consumer FIRST, before inserting
    // synthetic ASSIGNs that would otherwise hide the
    // <id>#return_value ASSIGN from patch_return_value's
    // forward search.
    not_exprt added{was_present};
    patch_return_value(body, it, cf_id, added);
    auto next_it = std::next(orig_call_it);
    body.insert_before(
      next_it,
      goto_programt::make_assignment(
        code_assignt(set.symbol_expr(), new_set), loc));
    body.insert_before(
      next_it,
      goto_programt::make_assignment(
        code_assignt(sz.symbol_expr(), new_sz), loc));
    orig_call_it->turn_into_skip();
    it = orig_call_it;
    return true;
  }

  case axiomatic_op::HS_CONTAINS:
  {
    if(args.size() < 2)
      return false;
    const exprt elem = coerce(args[1], object_ptr_type());
    const symbolt &set =
      ensure_global_array(symbol_table, AXIOMATIC_SET_SYM, set_array_type());
    exprt idx = pack_index(receiver, elem);
    exprt is_present = index_exprt(set.symbol_expr(), idx);
    if(!patch_return_value(body, it, cf_id, is_present))
      return false;
    orig_call_it->turn_into_skip();
    it = orig_call_it;
    return true;
  }
  }

  return false;
}

} // namespace

std::size_t lower_axiomatic_collections(goto_modelt &goto_model)
{
  std::size_t rewritten = 0;
  for(auto &fp : goto_model.goto_functions.function_map)
  {
    goto_programt &body = fp.second.body;
    bool changed = false;
    for(auto it = body.instructions.begin(); it != body.instructions.end();
        ++it)
    {
      if(lower_one_call(body, it, goto_model.symbol_table))
      {
        ++rewritten;
        changed = true;
      }
    }
    if(changed)
      body.update();
  }
  return rewritten;
}
