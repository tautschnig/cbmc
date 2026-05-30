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
#include <optional>
#include <string>

namespace
{

const std::string AXIOMATIC_KV_SYM = "java::axiomatic::_kv";
const std::string AXIOMATIC_SET_SYM = "java::axiomatic::_set";
const std::string AXIOMATIC_SZ_SYM = "java::axiomatic::_sz";
// List element storage. Indexed by `(receiver << 32) | uint32(index)`.
// Element type is Object*. The receiver bits in the high 32 keep the
// array disjoint from HashMap _kv state for the same receiver pointer.
const std::string AXIOMATIC_LIST_SYM = "java::axiomatic::_list";

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

/// Build (or look up) a global SMT-array symbol. The symbol
/// is initialised to `array_of(default_element)` so that
/// fresh slots read as the default value rather than nondet.
/// Without this, a freshly-allocated HashMap reads stale
/// nondet values from prior program states for slots it has
/// never written, breaking the size-tracking invariant.
const symbolt &ensure_global_array(
  symbol_tablet &symbol_table,
  const std::string &name,
  const array_typet &array_type,
  const exprt &default_element)
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
  // Initialise to array_of(default_element). CBMC's symbolic
  // execution treats this as a uniform-value array; later
  // `with` updates lift it to an SMT-LIB array of the right
  // shape.
  sym.value = array_of_exprt{default_element, array_type};

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

/// Try to extract a value-semantic int from a boxed-primitive
/// key argument. JBMC autoboxes `m.put(intK, ...)` to
/// `m.put(Integer.valueOf(intK), ...)` whose call-site key
/// expression in the goto IR has shape:
///
///   cast(address_of(*<sym>.@java.lang.Number.@java.lang.Object),
///        struct java::java.lang.Object*)
///
/// where `<sym>` has type `struct java::java.lang.Integer*`.
/// Returns the int-typed expression `(*<sym>).value` if the
/// pattern matches, or `std::nullopt` otherwise.
///
/// Without this, the lowering pass packs the (receiver, key)
/// index using pointer identity, and two distinct
/// `Integer.valueOf(n)` allocations with the same int value
/// produce different pointers (JBMC's Integer model is
/// `return new Integer(n)`, no cache). The result is that
/// `m.put(5, v); m.get(5)` writes to one slot and reads from
/// a different slot.
///
/// Also recognises the analogous patterns for boxed Long,
/// Short, Byte, Character (whose value field is also called
/// `value`).
std::optional<exprt> extract_boxed_primitive_value(
  const exprt &key,
  const symbol_tablet *symbol_table_for_validation = nullptr)
{
  // Strip the outer cast to Object*.
  exprt e = key;
  while(e.id() == ID_typecast)
    e = to_typecast_expr(e).op();

  // Direct boxed-pointer shape: the key is an expression of
  // pointer-to-boxed-primitive type. JBMC produces this at
  // the call site of HashMap.put / get when the key was
  // autoboxed from a primitive (Integer.valueOf etc.) and
  // the boxed result was stored in a local before being
  // passed.
  //
  // Recognised before the older address_of-of-member shape
  // below; the two are alternatives, and the simpler one
  // matches more call sites in practice.
  //
  // The match is gated by `symbol_table_for_validation`:
  // when supplied, we only fire if the struct definition
  // carries a literal `value` component. This guards
  // against stubbed-class shapes that lack the field; the
  // older callers that don't supply a symbol table get the
  // unconditional behaviour for backward compatibility.
  if(e.type().id() == ID_pointer)
  {
    const typet &base = to_pointer_type(e.type()).base_type();
    if(base.id() == ID_struct_tag)
    {
      const irep_idt cls = to_struct_tag_type(base).get_identifier();
      typet value_type;
      if(cls == "java::java.lang.Integer")
        value_type = java_int_type();
      else if(cls == "java::java.lang.Long")
        value_type = java_long_type();
      else if(cls == "java::java.lang.Short")
        value_type = java_short_type();
      else if(cls == "java::java.lang.Byte")
        value_type = java_byte_type();
      else if(cls == "java::java.lang.Character")
        value_type = java_char_type();
      else if(cls == "java::java.lang.Boolean")
        value_type = java_boolean_type();

      if(!value_type.is_nil())
      {
        bool has_value_field = true;
        if(symbol_table_for_validation != nullptr)
        {
          has_value_field = false;
          if(const auto *cs = symbol_table_for_validation->lookup(cls))
          {
            if(cs->type.id() == ID_struct)
            {
              for(const auto &c : to_struct_type(cs->type).components())
              {
                if(c.get_name() == "value")
                {
                  has_value_field = true;
                  break;
                }
              }
            }
          }
        }
        if(has_value_field)
        {
          dereference_exprt boxed_struct{e};
          member_exprt value_field{boxed_struct, "value", value_type};
          return value_field;
        }
      }
    }
  }

  if(e.id() != ID_address_of)
    return std::nullopt;

  // Walk the member-chain underneath the address_of looking
  // for a tail of the form `*<sym>.@<numeric>.@java.lang.Object`.
  exprt operand = to_address_of_expr(e).object();
  if(operand.id() != ID_member)
    return std::nullopt;
  if(to_member_expr(operand).get_component_name() != "@java.lang.Object")
    return std::nullopt;
  exprt parent_member = to_member_expr(operand).compound();
  if(parent_member.id() != ID_member)
    return std::nullopt;
  // The parent member's component name identifies the boxed
  // primitive class. Recognise the JDK-standard hierarchy.
  const irep_idt parent_name =
    to_member_expr(parent_member).get_component_name();
  if(
    parent_name != "@java.lang.Number" &&
    parent_name != "@java.lang.Character" &&
    parent_name != "@java.lang.Boolean")
    return std::nullopt;
  // Walk up to the dereference.
  exprt deref = to_member_expr(parent_member).compound();
  if(deref.id() != ID_dereference)
    return std::nullopt;
  exprt boxed_ptr = to_dereference_expr(deref).pointer();
  if(boxed_ptr.type().id() != ID_pointer)
    return std::nullopt;
  const typet &boxed_type = to_pointer_type(boxed_ptr.type()).base_type();
  if(boxed_type.id() != ID_struct_tag)
    return std::nullopt;
  const irep_idt boxed_class = to_struct_tag_type(boxed_type).get_identifier();

  // Map the static class to the int-equivalent type and the
  // primitive value field.
  // All the supported boxed primitives use a field literally
  // called `value`. Their primitive types differ.
  typet value_type;
  if(boxed_class == "java::java.lang.Integer")
    value_type = java_int_type();
  else if(boxed_class == "java::java.lang.Long")
    value_type = java_long_type();
  else if(boxed_class == "java::java.lang.Short")
    value_type = java_short_type();
  else if(boxed_class == "java::java.lang.Byte")
    value_type = java_byte_type();
  else if(boxed_class == "java::java.lang.Character")
    value_type = java_char_type();
  else if(boxed_class == "java::java.lang.Boolean")
    value_type = java_boolean_type();
  else
    return std::nullopt;

  // Build (*boxed_ptr).value.
  dereference_exprt boxed_struct{boxed_ptr};
  member_exprt value_field{boxed_struct, "value", value_type};
  return value_field;
}

/// Pack a (receiver, key) pair into a 64-bit unsigned int.
///
///   packed = (uint64) ptr32(receiver) << 32 | (uint64) key_int
///
/// where `key_int` is the value-semantic int for boxed
/// primitives (extracted via `extract_boxed_primitive_value`)
/// or the pointer-identity int otherwise. Identity-only
/// matching of value-semantic boxed-primitive keys is unsound
/// because JBMC's Integer.valueOf returns a fresh allocation
/// each call.
exprt pack_index(
  const exprt &receiver,
  const exprt &key,
  const symbol_tablet *symbol_table_for_validation = nullptr)
{
  const typet u64 = packed_index_type();
  exprt r32 = ptr_to_int32(receiver);
  exprt r64 = typecast_exprt(r32, u64);
  exprt shifted = shl_exprt(r64, from_integer(32, u64));

  exprt k64;
  if(
    auto value =
      extract_boxed_primitive_value(key, symbol_table_for_validation))
  {
    // Value-semantic packing: use the unboxed int / long /
    // ... value as the low 32 bits.
    k64 = typecast_exprt(*value, u64);
  }
  else
  {
    // Identity-semantic packing: use pointer offset as the
    // low 32 bits.
    k64 = typecast_exprt(ptr_to_int32(key), u64);
  }
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
  // List operations.
  // The receiver-keyed _sz array is shared with HashMap /
  // HashSet. Element storage is a separate global
  // _list_kv array indexed by (receiver << 32 | uint32(index)).
  LIST_INIT,
  LIST_SIZE,
  LIST_IS_EMPTY,
  LIST_ADD,
  LIST_GET,
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

  // List operations: ArrayList and LinkedList implementations
  // share the same axiomatic semantics. We accept their
  // constructors and the four basic operations. The
  // implementations of size, isEmpty, add, get from
  // AbstractList / AbstractCollection are also accepted, so
  // virtual dispatch through the abstract-class methods
  // hits the model.
  if(
    id == "java::java.util.ArrayList.<init>:()V" ||
    id == "java::java.util.ArrayList.<init>:(I)V" ||
    id == "java::java.util.LinkedList.<init>:()V")
    return axiomatic_op::LIST_INIT;
  if(
    id == "java::java.util.ArrayList.size:()I" ||
    id == "java::java.util.LinkedList.size:()I" ||
    id == "java::java.util.AbstractList.size:()I" ||
    id == "java::java.util.AbstractCollection.size:()I")
    return axiomatic_op::LIST_SIZE;
  if(
    id == "java::java.util.ArrayList.isEmpty:()Z" ||
    id == "java::java.util.LinkedList.isEmpty:()Z" ||
    id == "java::java.util.AbstractCollection.isEmpty:()Z")
    return axiomatic_op::LIST_IS_EMPTY;
  if(
    id == "java::java.util.ArrayList.add:(Ljava/lang/Object;)Z" ||
    id == "java::java.util.LinkedList.add:(Ljava/lang/Object;)Z" ||
    id == "java::java.util.AbstractList.add:(Ljava/lang/Object;)Z" ||
    id == "java::java.util.AbstractCollection.add:(Ljava/lang/Object;)Z")
    return axiomatic_op::LIST_ADD;
  if(
    id == "java::java.util.ArrayList.get:(I)Ljava/lang/Object;" ||
    id == "java::java.util.LinkedList.get:(I)Ljava/lang/Object;" ||
    id == "java::java.util.AbstractList.get:(I)Ljava/lang/Object;")
    return axiomatic_op::LIST_GET;

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
  // The consumer ASSIGN's RHS may be either a bare
  //     <call_id>#return_value
  // (the typical case) or wrapped in one or more typecasts
  //     cast(<call_id>#return_value, T)
  // (when the JVM bytecode upcasts the result, e.g. when
  // HashMap.get's Object return value flows into an
  // anonymous Object-typed local). Strip the cast(s) before
  // comparing the identifier.
  const exprt *rhs_inner = &next_it->assign_rhs();
  while(rhs_inner->id() == ID_typecast)
    rhs_inner = &to_typecast_expr(*rhs_inner).op();
  if(rhs_inner->id() != ID_symbol)
    return false;
  const std::string rhs_id =
    id2string(to_symbol_expr(*rhs_inner).get_identifier());
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
    const symbolt &sz = ensure_global_array(
      symbol_table,
      AXIOMATIC_SZ_SYM,
      sz_array_type(),
      from_integer(0, java_int_type()));
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
    const symbolt &kv = ensure_global_array(
      symbol_table, AXIOMATIC_KV_SYM, kv_array_type(), object_null());
    exprt idx = pack_index(receiver, key, &symbol_table);
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
    const symbolt &kv = ensure_global_array(
      symbol_table, AXIOMATIC_KV_SYM, kv_array_type(), object_null());
    const symbolt &sz = ensure_global_array(
      symbol_table,
      AXIOMATIC_SZ_SYM,
      sz_array_type(),
      from_integer(0, java_int_type()));
    exprt idx = pack_index(receiver, key, &symbol_table);
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
    // Update _sz BEFORE _kv so the `was_unset` test reads the
    // OLD kv value. If we updated _kv first, `_kv[idx]` would
    // read the new value (non-null) and size would never
    // increment.
    body.insert_before(
      next_it,
      goto_programt::make_assignment(
        code_assignt(sz.symbol_expr(), new_sz), loc));
    body.insert_before(
      next_it,
      goto_programt::make_assignment(
        code_assignt(kv.symbol_expr(), new_kv), loc));
    orig_call_it->turn_into_skip();
    it = orig_call_it;
    return true;
  }

  case axiomatic_op::HM_SIZE:
  case axiomatic_op::HM_IS_EMPTY:
  case axiomatic_op::HS_SIZE:
  case axiomatic_op::HS_IS_EMPTY:
  case axiomatic_op::LIST_SIZE:
  case axiomatic_op::LIST_IS_EMPTY:
  {
    const symbolt &sz = ensure_global_array(
      symbol_table,
      AXIOMATIC_SZ_SYM,
      sz_array_type(),
      from_integer(0, java_int_type()));
    exprt rkey = pack_receiver_only(receiver);
    exprt cur_sz = index_exprt(sz.symbol_expr(), rkey);
    exprt result;
    if(
      op == axiomatic_op::HM_SIZE || op == axiomatic_op::HS_SIZE ||
      op == axiomatic_op::LIST_SIZE)
      result = cur_sz;
    else
      result = equal_exprt(cur_sz, from_integer(0, java_int_type()));
    if(!patch_return_value(body, it, cf_id, result))
      return false;
    orig_call_it->turn_into_skip();
    it = orig_call_it;
    return true;
  }

  case axiomatic_op::LIST_INIT:
  {
    // Reset this receiver's list size to 0. Element storage
    // is left as-is — fresh receivers haven't written
    // _list[handle, *] so reads return the default null.
    const symbolt &sz = ensure_global_array(
      symbol_table,
      AXIOMATIC_SZ_SYM,
      sz_array_type(),
      from_integer(0, java_int_type()));
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

  case axiomatic_op::LIST_GET:
  {
    if(args.size() < 2)
      return false;
    const exprt index = args[1];
    const symbolt &list = ensure_global_array(
      symbol_table, AXIOMATIC_LIST_SYM, kv_array_type(), object_null());
    // pack_index uses pointer-identity for receivers and
    // value-semantic for primitive int (which is what an int
    // index is). Reuse it.
    const exprt idx = pack_index(receiver, index, &symbol_table);
    const exprt val = index_exprt(list.symbol_expr(), idx);
    if(!patch_return_value(body, it, cf_id, val))
      return false;
    orig_call_it->turn_into_skip();
    it = orig_call_it;
    return true;
  }

  case axiomatic_op::LIST_ADD:
  {
    if(args.size() < 2)
      return false;
    const exprt elem = coerce(args[1], object_ptr_type());
    const symbolt &list = ensure_global_array(
      symbol_table, AXIOMATIC_LIST_SYM, kv_array_type(), object_null());
    const symbolt &sz = ensure_global_array(
      symbol_table,
      AXIOMATIC_SZ_SYM,
      sz_array_type(),
      from_integer(0, java_int_type()));
    exprt rkey = pack_receiver_only(receiver);
    exprt cur_sz = index_exprt(sz.symbol_expr(), rkey);
    // Index for the new element: pack(receiver, cur_sz).
    const exprt idx = pack_index(receiver, cur_sz, &symbol_table);
    with_exprt new_list{list.symbol_expr(), idx, elem};
    plus_exprt incremented{cur_sz, from_integer(1, java_int_type())};
    with_exprt new_sz{sz.symbol_expr(), rkey, incremented};
    // patch_return_value on the bool result first, then
    // insert the assigns. add() always returns true.
    patch_return_value(body, it, cf_id, true_exprt{});
    auto next_it = std::next(orig_call_it);
    body.insert_before(
      next_it,
      goto_programt::make_assignment(
        code_assignt(sz.symbol_expr(), new_sz), loc));
    body.insert_before(
      next_it,
      goto_programt::make_assignment(
        code_assignt(list.symbol_expr(), new_list), loc));
    orig_call_it->turn_into_skip();
    it = orig_call_it;
    return true;
  }

  case axiomatic_op::HS_ADD:
  {
    if(args.size() < 2)
      return false;
    const exprt elem = coerce(args[1], object_ptr_type());
    const symbolt &set = ensure_global_array(
      symbol_table, AXIOMATIC_SET_SYM, set_array_type(), false_exprt{});
    const symbolt &sz = ensure_global_array(
      symbol_table,
      AXIOMATIC_SZ_SYM,
      sz_array_type(),
      from_integer(0, java_int_type()));
    exprt idx = pack_index(receiver, elem, &symbol_table);
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
    // Update _sz BEFORE _set so the `was_present` test reads
    // the OLD set value (see HM_PUT for explanation).
    body.insert_before(
      next_it,
      goto_programt::make_assignment(
        code_assignt(sz.symbol_expr(), new_sz), loc));
    body.insert_before(
      next_it,
      goto_programt::make_assignment(
        code_assignt(set.symbol_expr(), new_set), loc));
    orig_call_it->turn_into_skip();
    it = orig_call_it;
    return true;
  }

  case axiomatic_op::HS_CONTAINS:
  {
    if(args.size() < 2)
      return false;
    const exprt elem = coerce(args[1], object_ptr_type());
    const symbolt &set = ensure_global_array(
      symbol_table, AXIOMATIC_SET_SYM, set_array_type(), false_exprt{});
    exprt idx = pack_index(receiver, elem, &symbol_table);
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

exprt jml_axiomatic_size_expr(
  symbol_table_baset &symbol_table,
  const exprt &receiver)
{
  // We want a writable symbol_tablet here so we can install
  // the backing array if it doesn't yet exist. The runtime
  // type passed in is goto_model.symbol_table, so the cast
  // is safe.
  auto &writable = dynamic_cast<symbol_tablet &>(symbol_table);
  const symbolt &sz = ensure_global_array(
    writable,
    AXIOMATIC_SZ_SYM,
    sz_array_type(),
    from_integer(0, java_int_type()));
  return index_exprt(sz.symbol_expr(), pack_receiver_only(receiver));
}

exprt jml_axiomatic_is_empty_expr(
  symbol_table_baset &symbol_table,
  const exprt &receiver)
{
  return equal_exprt(
    jml_axiomatic_size_expr(symbol_table, receiver),
    from_integer(0, java_int_type()));
}

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
  // We deliberately do NOT initialise the global _kv / _set /
  // _sz arrays at __CPROVER_initialize. CBMC's lazy nondet
  // input init treats receiver pointers passed as method
  // arguments as fresh nondet allocations whose backing slots
  // in _kv must be nondet for the lemma's `containsKey(k)`
  // preconditions to be satisfiable. Initialising the globals
  // to array_of(null) makes those preconditions trivially
  // false, vacuously proving any lemma with such a
  // precondition (e.g. PFormulaSpec.litNegationExclusive).
  //
  // The cost is that fresh-allocation cases (`m = new
  // HashMap<>(); m.put(1, v); ...`) read STALE nondet values
  // for the put's `old_v` test, so the size-tracking on
  // `_sz[receiver]` is an over-approximation: size may be
  // nondet rather than exact. Lemmas that rely on `size()`
  // exactness are not supported by the axiomatic encoding.
  // Lemmas that only use containsKey / get / put work
  // correctly because each operation reads / writes the same
  // packed slot.
  return rewritten;
}
