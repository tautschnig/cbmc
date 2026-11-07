/*******************************************************************\

Module: Go front-end: strings and slices

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// Conversion of strings and slices (Go1.27 §String_types, §Slice_types)
/// and of the built-in functions on them (§Appending_and_copying_slices,
/// §Making_slices_maps_and_channels, §Clear).
///
/// Representation (doc/architectural/go-frontend-plan.md §5):
///
/// - A string is `{data *uint8, len int}`, a slice `{data *E, len int, cap
///   int}`. The data pointer points into an ordinary array: a static array
///   for each string constant, or a dynamic object allocated where a string
///   or underlying array is produced (each execution allocates anew).
///   Every component has a fixed width, so strings and slices may be
///   stored in structs, arrays and other slices.
/// - Element ranges are copied without loops, by __CPROVER_array_copy into
///   a new array of the exact length followed by __CPROVER_array_replace,
///   as CBMC's model of memcpy does. This copy is independent of overlaps.
/// - Operations that need loops (string equality and ordering, UTF-8
///   decoding) are Go functions in the runtime package cprover/rt
///   (helper/runtime/rt.go), converted with the program.

#include <util/arith_tools.h>
#include <util/c_types.h>
#include <util/config.h>
#include <util/expr_initializer.h>
#include <util/expr_util.h>
#include <util/mp_arith.h>
#include <util/pointer_expr.h>
#include <util/pointer_offset_size.h>
#include <util/prefix.h>
#include <util/simplify_expr.h>
#include <util/std_code.h>
#include <util/symbol_table_base.h>

#include "go_convert.h"
#include "go_parse_tree.h"

#include <algorithm>
#include <cstring>

/// The type of lengths, capacities and indices: int.
static signedbv_typet int_type()
{
  return signedbv_typet{config.go.int_width};
}

/// An expression statement evaluating \p value, at \p location.
static code_expressiont
expression_statement(exprt value, const source_locationt &location)
{
  code_expressiont result{std::move(value)};
  result.add_source_location() = location;
  return result;
}

/// The largest int.
static mp_integer max_int()
{
  return power(2, config.go.int_width - 1) - 1;
}

/// \p expr with location \p location.
static exprt at(exprt expr, const source_locationt &location)
{
  expr.add_source_location() = location;
  return expr;
}

/// The data, len and cap components of string or slice \p value.
static member_exprt component(const exprt &value, const char *name)
{
  const struct_typet &type = to_struct_type(value.type());
  return member_exprt{value, type.get_component(name)};
}

exprt go_convertert::string_constant(
  const std::string &bytes,
  const source_locationt &location)
{
  const struct_typet type = go_type_convertert::string_type();
  const pointer_typet &data_type =
    to_pointer_type(type.get_component("data").type());
  if(bytes.empty())
  {
    // The zero value; no bytes need to be stored.
    return at(
      struct_exprt{
        {null_pointer_exprt{data_type}, from_integer(0, int_type())}, type},
      location);
  }
  // One static array per distinct constant.
  std::string name = "go::$string";
  for(const unsigned char c : bytes)
    name += "_" + std::to_string(c);
  if(name.size() > 200)
    name = "go::$string#" + std::to_string(std::hash<std::string>{}(bytes));
  // Go1.27 §String_types: "Strings are immutable": the bytes are
  // const-qualified, so that goto-symex keeps the array whole (see
  // field_sensitivityt) rather than assigning each byte.
  typet byte_type = unsignedbv_typet{8};
  byte_type.set(ID_C_constant, true);
  const array_typet array_type{
    byte_type, from_integer(bytes.size(), c_index_type())};
  if(!symbol_table.has_symbol(name))
  {
    exprt::operandst elements;
    for(const unsigned char c : bytes)
      elements.push_back(from_integer(c, byte_type));
    symbolt symbol{name, array_type, "go"};
    symbol.base_name = name;
    symbol.is_static_lifetime = true;
    symbol.is_lvalue = true;
    symbol.value = array_exprt{std::move(elements), array_type};
    symbol_table.add(symbol);
  }
  const symbol_exprt array{name, array_type};
  return at(
    struct_exprt{
      {address_of_exprt{
         index_exprt{array, from_integer(0, c_index_type())}, data_type},
       from_integer(bytes.size(), int_type())},
      type},
    location);
}

exprt go_convertert::allocate_elements(
  const typet &element,
  const exprt &count,
  bool zero,
  const source_locationt &location,
  code_blockt &dest)
{
  // A dynamic object of count elements (symbolic counts are fine), zeroed
  // or arbitrary. As C's malloc, allocate count * sizeof(E) bytes through a
  // void pointer: goto-symex then derives the type E[count] from the
  // annotated size and gives the object a size variable of its own (a
  // typed allocation would keep count, which may change, in the object's
  // type). With zero, all bits are zero, which is the zero value of every
  // type supported so far (nil pointers are null).
  auto element_size = size_of_expr(element, ns);
  CHECK_RETURN(element_size.has_value());
  element_size->set(ID_C_c_sizeof_type, element);
  const exprt n = typecast_exprt::conditional_cast(count, size_type());
  exprt size = n.is_constant() && to_constant_expr(n).is_zero()
                 ? exprt{from_integer(0, size_type())}
                 : exprt{mult_exprt{std::move(*element_size), n}};
  side_effect_exprt allocate{
    ID_allocate,
    // Elements with pointers are zeroed even when they are set right away:
    // the value set of an arbitrary value has all objects in it, which the
    // value sets of objects shared between goroutines would keep.
    {std::move(size),
     zero || contains_reference(element, ns, true) ? exprt{true_exprt{}}
                                                   : exprt{false_exprt{}}},
    pointer_typet{empty_typet{}, config.ansi_c.pointer_width},
    location};
  const pointer_typet element_pointer{element, config.ansi_c.pointer_width};
  symbol_exprt data = new_temporary(element_pointer, location, dest);
  dest.add(
    code_frontend_assignt{data, typecast_exprt{allocate, element_pointer}});
  return std::move(data);
}

void go_convertert::copy_elements(
  const exprt &dest,
  const exprt &source,
  const exprt &count,
  const source_locationt &location,
  code_blockt &dest_code,
  bool race_ranges)
{
  // Copy through a new array of exactly count elements, which makes the
  // result "independent of whether the memory referenced by the arguments
  // overlaps" (Go1.27 §Appending_and_copying_slices). The pointers are plain
  // variables, as in the C library's memcpy: the array operations resolve
  // them through the value sets of symbols.
  const typet &element = to_pointer_type(dest.type()).base_type();
  code_blockt copy;
  const exprt to = evaluate_once(dest, location, copy);
  const exprt from = evaluate_once(
    typecast_exprt::conditional_cast(source, dest.type()), location, copy);
  const exprt buffer = evaluate_once(
    allocate_elements(element, count, false, location, copy), location, copy);
  // Without loops, as CBMC's model of memcpy: __CPROVER_array_copy into the
  // new array, then __CPROVER_array_replace. These copy bytes, also of
  // elements with pointers: the byte operators between arrays of the same
  // element type are lowered element by element (lower_byte_operators.cpp),
  // so the pointers keep what they point to.
  //
  // For the data-race check, the accesses are of the count elements at
  // either pointer (rather than of the whole objects, as for other bulk
  // operations), unless their size is a constant too large to flag byte by
  // byte. The new array is not shared.
  const auto element_size = size_of_expr(element, ns);
  CHECK_RETURN(element_size.has_value());
  const exprt bytes = simplify_expr(
    mult_exprt{
      typecast_exprt::conditional_cast(count, size_type()),
      typecast_exprt::conditional_cast(*element_size, size_type())},
    ns);
  const auto constant_bytes = numeric_cast<mp_integer>(bytes);
  const bool ranges = race_check && race_ranges &&
                      (!constant_bytes.has_value() || *constant_bytes <= 1024);
  if(ranges)
    race_range_marker("go::$race_read_range", from, bytes, location, copy);
  codet array_copy{ID_array_copy, {buffer, from}};
  array_copy.add_source_location() = location;
  if(ranges)
    array_copy.set(ID_C_go_race_ranges, true);
  copy.add(std::move(array_copy));
  if(ranges)
    race_range_marker("go::$race_write_range", to, bytes, location, copy);
  codet array_replace{ID_array_replace, {to, buffer}};
  array_replace.add_source_location() = location;
  if(ranges)
    array_replace.set(ID_C_go_race_ranges, true);
  copy.add(std::move(array_replace));
  code_ifthenelset if_any{
    binary_relation_exprt{count, ID_gt, from_integer(0, count.type())},
    std::move(copy)};
  if_any.add_source_location() = location;
  dest_code.add(std::move(if_any));
}

exprt go_convertert::runtime_call(
  const std::string &name,
  exprt::operandst arguments,
  const source_locationt &location)
{
  const symbolt &function = ns.lookup("go::cprover/rt." + name);
  const code_typet &code = to_code_type(function.type);
  for(std::size_t i = 0; i < arguments.size(); ++i)
  {
    arguments[i] = typecast_exprt::conditional_cast(
      arguments[i], code.parameters()[i].type());
  }
  return side_effect_expr_function_callt{
    function.symbol_expr(), std::move(arguments), code.return_type(), location};
}

/// The bytes of \p value if it is a string constant (string_constant, after
/// conversions), or nothing.
static std::optional<std::string>
constant_string_bytes(const exprt &value, const namespacet &ns)
{
  const exprt *e = &value;
  while(e->id() == ID_typecast)
    e = &to_typecast_expr(*e).op();
  if(e->id() != ID_struct || e->operands().size() != 2)
    return {};
  const exprt &data = e->operands()[0];
  const auto length = numeric_cast<mp_integer>(e->operands()[1]);
  if(!length.has_value())
    return {};
  if(*length == 0 && data.id() == ID_constant)
    return std::string{};
  if(
    data.id() != ID_address_of ||
    to_address_of_expr(data).object().id() != ID_index)
  {
    return {};
  }
  const exprt &array = to_index_expr(to_address_of_expr(data).object()).array();
  const symbolt *symbol;
  if(
    array.id() != ID_symbol ||
    !has_prefix(
      id2string(to_symbol_expr(array).get_identifier()), "go::$string") ||
    ns.lookup(to_symbol_expr(array).get_identifier(), symbol) ||
    symbol->value.id() != ID_array ||
    symbol->value.operands().size() != *length)
  {
    return {};
  }
  std::string bytes;
  for(const auto &element : symbol->value.operands())
  {
    const auto byte = numeric_cast<unsigned>(element);
    if(!byte.has_value())
      return {};
    bytes.push_back(static_cast<char>(*byte));
  }
  return bytes;
}

exprt go_convertert::string_equal(
  const exprt &a,
  const exprt &b,
  const source_locationt &location)
{
  // Go1.27 §Comparison_operators: "String types are comparable and
  // ordered. Two string values are compared lexically byte-wise."
  // Against a short constant: byte by byte (the bytes are read only if the
  // lengths are equal), without a loop.
  for(const auto &[constant, other] : {std::pair{&a, &b}, std::pair{&b, &a}})
  {
    const auto bytes = constant_string_bytes(*constant, ns);
    if(!bytes.has_value() || bytes->size() > 64)
      continue;
    code_blockt block;
    const exprt value = evaluate_once(*other, location, block);
    exprt::operandst conjuncts{equal_exprt{
      component(value, "len"), from_integer(bytes->size(), int_type())}};
    for(std::size_t i = 0; i < bytes->size(); ++i)
    {
      const exprt byte = dereference_exprt{
        plus_exprt{component(value, "data"), from_integer(i, c_index_type())}};
      conjuncts.push_back(equal_exprt{
        byte,
        from_integer(static_cast<unsigned char>((*bytes)[i]), byte.type())});
    }
    return with_prefix(conjunction(std::move(conjuncts)), block, location);
  }
  // Otherwise with a loop (rt.StringEqual). Comparing copies of the bytes
  // as arrays (__CPROVER_array_equal of arrays of a common symbolic
  // length) needs no loop, but made programs that compare many strings
  // (encoding/json) forty times slower.
  return as_condition(runtime_call("StringEqual", {a, b}, location));
}

exprt go_convertert::interface_string_equal(
  const exprt &iface,
  const exprt &string,
  const source_locationt &location)
{
  // Go1.27 §Comparison_operators: "A value x of non-interface type X and a
  // value t of interface type T can be compared if type X is comparable and
  // X implements T. They are equal if t's dynamic type is identical to X and
  // t's dynamic value is equal to x." For a short constant x (as in
  // `recover() == "message"`), so that string_equal compares its bytes
  // without a loop.
  const auto bytes = constant_string_bytes(string, ns);
  if(!bytes.has_value() || bytes->size() > 64)
    return nil_exprt{};
  std::size_t tid = go_type_id(string.type());
  if(tid == 0)
    tid = type_converter.basic_type_id("string");
  code_blockt block;
  const exprt value = evaluate_once(iface, location, block);
  const struct_typet iface_type = go_type_convertert::interface_type();
  const member_exprt type{value, iface_type.components()[0]};
  return with_prefix(
    and_exprt{
      equal_exprt{type, from_integer(tid, type.type())},
      string_equal(unbox(value, string.type()), string, location)},
    block,
    location);
}

exprt go_convertert::string_concat(
  const exprt &a,
  const exprt &b,
  const source_locationt &location)
{
  // Go1.27 §String_concatenation: "String addition creates a new string by
  // concatenating the operands."
  code_blockt block;
  const exprt x = evaluate_once(a, location, block);
  const exprt y = evaluate_once(b, location, block);
  const symbol_exprt length = new_temporary(int_type(), location, block);
  block.add(code_frontend_assignt{
    length, plus_exprt{component(x, "len"), component(y, "len")}});
  const exprt data =
    allocate_elements(unsignedbv_typet{8}, length, false, location, block);
  const symbol_exprt data_tmp = new_temporary(data.type(), location, block);
  block.add(code_frontend_assignt{data_tmp, data});
  copy_elements(
    data_tmp, component(x, "data"), component(x, "len"), location, block);
  copy_elements(
    plus_exprt{data_tmp, component(x, "len")},
    component(y, "data"),
    component(y, "len"),
    location,
    block);
  const struct_typet type = go_type_convertert::string_type();
  // The type of the operands, which records the Go type (go_type_id).
  return with_prefix(
    struct_exprt{{data_tmp, length}, x.type()}, block, location);
}

exprt go_convertert::nondet_value(
  const typet &type,
  const source_locationt &location,
  code_blockt &dest,
  std::size_t depth)
{
  // An arbitrary value of the type, for cprover.Nondet and the arguments
  // of the entry point. Strings and slices of values without references
  // have any length and arbitrary contents in a new underlying array.
  // Values with references are built as CBMC's object factory builds C
  // inputs (--max-nondet-tree-depth, --min-null-tree-depth): a pointer is
  // nil or points to a new arbitrary value, a map is nil or has at most
  // max_nondet_array_length arbitrary entries, a slice of such elements
  // has at most that capacity, and an interface value is nil or holds an
  // arbitrary value of one of the program's types that implement it (see
  // finish_nondet_interfaces); beyond the maximum depth, they are nil.
  // Objects are not shared between references. Function values and
  // channels are not supported.
  if(type.id() == ID_c_bool)
  {
    // false or true, not an arbitrary byte.
    return typecast_exprt{
      side_effect_expr_nondett{bool_typet{}, location}, type};
  }
  const bool references = contains_reference(type, ns, false);
  const bool unsafe_pointer =
    type.id() == ID_pointer &&
    to_pointer_type(type).base_type().id() == ID_empty;
  if(is_func_value(type) || is_go_chan(type) || unsafe_pointer)
  {
    // An arbitrary unsafe.Pointer may point to an object of any type.
    unsupported(
      location,
      std::string{"cprover.Nondet and --function arguments of "} +
        (is_go_chan(type) ? "channel types"
         : unsafe_pointer ? "type unsafe.Pointer"
                          : "function types"));
    return nil_exprt{};
  }
  const bool beyond_depth = depth >= nondet_parameters.max_nondet_tree_depth;
  // Whether a reference may be nil at this depth.
  const auto may_be_nil = [&]() -> exprt
  {
    if(beyond_depth)
      return true_exprt{};
    if(depth < nondet_parameters.min_null_tree_depth)
      return false_exprt{};
    return side_effect_expr_nondett{bool_typet{}, location};
  };
  if(type.id() == ID_pointer && !is_go_map(type))
  {
    // Go1.27 §Pointer_types: nil or the address of a variable.
    const pointer_typet &pointer = to_pointer_type(type);
    const symbol_exprt result = new_temporary(type, location, dest);
    dest.add(code_frontend_assignt{result, null_pointer_exprt{pointer}});
    if(beyond_depth)
      return result;
    code_blockt target;
    const exprt value =
      nondet_value(pointer.base_type(), location, target, depth + 1);
    if(value.is_nil())
      return nil_exprt{};
    const symbol_exprt object =
      new_heap_variable(pointer.base_type(), value, location, target);
    target.add(code_frontend_assignt{
      result, typecast_exprt::conditional_cast(object, type)});
    dest.add(code_ifthenelset{not_exprt{may_be_nil()}, std::move(target)});
    return result;
  }
  if(is_go_map(type))
  {
    // Go1.27 §Map_types: nil, or a map with some entries.
    const symbol_exprt result = new_temporary(type, location, dest);
    dest.add(
      code_frontend_assignt{result, null_pointer_exprt{to_pointer_type(type)}});
    if(beyond_depth)
      return result;
    code_blockt entries;
    entries.add(
      code_frontend_assignt{result, make_map(type, location, entries)});
    // Room for all entries from the start, so that the arrays never grow
    // (to sizes that would depend on the number of entries).
    const std::size_t max = nondet_parameters.max_nondet_array_length;
    const struct_typet &header =
      to_struct_type(to_pointer_type(type).base_type());
    const typet &key_type =
      to_pointer_type(header.get_component("keys").type()).base_type();
    const typet &value_type =
      to_pointer_type(header.get_component("vals").type()).base_type();
    const dereference_exprt map{result};
    entries.add(code_frontend_assignt{
      member_exprt{map, header.get_component("keys")},
      allocate_elements(
        key_type, from_integer(max, int_type()), false, location, entries)});
    entries.add(code_frontend_assignt{
      member_exprt{map, header.get_component("vals")},
      allocate_elements(
        value_type, from_integer(max, int_type()), false, location, entries)});
    entries.add(code_frontend_assignt{
      member_exprt{map, header.get_component("cap")},
      from_integer(max, int_type())});
    const symbol_exprt count = new_temporary(int_type(), location, entries);
    entries.add(code_frontend_assignt{
      count, side_effect_expr_nondett{int_type(), location}});
    entries.add(code_assumet{and_exprt{
      binary_relation_exprt{count, ID_ge, from_integer(0, int_type())},
      binary_relation_exprt{count, ID_le, from_integer(max, int_type())}}});
    // count entries with distinct keys (Go1.27 §Map_types: "A map is an
    // unordered group of elements of one type, called the element type,
    // indexed by a set of unique keys of another type").
    const exprt keys = member_exprt{map, header.get_component("keys")};
    const exprt vals = member_exprt{map, header.get_component("vals")};
    for(std::size_t i = 0; i < max; ++i)
    {
      code_blockt entry;
      const exprt key = evaluate_once(
        nondet_value(key_type, location, entry, depth + 1), location, entry);
      const exprt value = nondet_value(value_type, location, entry, depth + 1);
      if(key.is_nil() || value.is_nil())
        return nil_exprt{};
      for(std::size_t j = 0; j < i; ++j)
      {
        const exprt other =
          dereference_exprt{plus_exprt{keys, from_integer(j, int_type())}};
        entry.add(
          code_assumet{not_exprt{as_condition(equality(key, other, irept{}))}});
      }
      entry.add(code_frontend_assignt{
        dereference_exprt{plus_exprt{keys, from_integer(i, int_type())}}, key});
      entry.add(code_frontend_assignt{
        dereference_exprt{plus_exprt{vals, from_integer(i, int_type())}},
        value});
      entries.add(code_ifthenelset{
        binary_relation_exprt{from_integer(i, int_type()), ID_lt, count},
        std::move(entry)});
    }
    entries.add(code_frontend_assignt{
      member_exprt{map, header.get_component("len")}, count});
    dest.add(code_ifthenelset{not_exprt{may_be_nil()}, std::move(entries)});
    return result;
  }
  if(is_go_interface(type))
    return nondet_interface(type, location, depth);
  if(is_go_slice(type) && references)
  {
    // At most max_nondet_array_length elements (Go1.27
    // §Length_and_capacity: "0 <= len(s) <= cap(s)"), each arbitrary up
    // to the capacity; nil if the capacity is 0, possibly.
    const struct_typet &struct_type = to_struct_type(type);
    const typet &element =
      to_pointer_type(struct_type.get_component("data").type()).base_type();
    const std::size_t max = nondet_parameters.max_nondet_array_length;
    const symbol_exprt length = new_temporary(int_type(), location, dest);
    const symbol_exprt cap = new_temporary(int_type(), location, dest);
    dest.add(code_frontend_assignt{
      length, side_effect_expr_nondett{int_type(), location}});
    dest.add(code_frontend_assignt{
      cap, side_effect_expr_nondett{int_type(), location}});
    dest.add(code_assumet{conjunction(
      {binary_relation_exprt{length, ID_ge, from_integer(0, int_type())},
       binary_relation_exprt{length, ID_le, cap},
       binary_relation_exprt{
         cap, ID_le, from_integer(beyond_depth ? 0 : max, int_type())}})});
    const exprt data = evaluate_once(
      allocate_elements(element, cap, true, location, dest), location, dest);
    for(std::size_t i = 0; !beyond_depth && i < max; ++i)
    {
      code_blockt init;
      const exprt value = nondet_value(element, location, init, depth + 1);
      if(value.is_nil())
        return nil_exprt{};
      init.add(code_frontend_assignt{
        dereference_exprt{plus_exprt{data, from_integer(i, int_type())}},
        value});
      dest.add(code_ifthenelset{
        binary_relation_exprt{from_integer(i, int_type()), ID_lt, cap},
        std::move(init)});
    }
    const symbol_exprt is_nil = new_temporary(bool_typet{}, location, dest);
    dest.add(code_frontend_assignt{
      is_nil, side_effect_expr_nondett{bool_typet{}, location}});
    dest.add(code_assumet{or_exprt{
      not_exprt{is_nil}, equal_exprt{cap, from_integer(0, int_type())}}});
    return struct_exprt{
      {if_exprt{is_nil, null_pointer_exprt{to_pointer_type(data.type())}, data},
       length,
       cap},
      type};
  }
  if(is_go_string(type) || is_go_slice(type))
  {
    const struct_typet &struct_type = to_struct_type(type);
    const typet &element =
      to_pointer_type(struct_type.get_component("data").type()).base_type();
    const symbol_exprt length = new_temporary(int_type(), location, dest);
    dest.add(code_frontend_assignt{
      length, side_effect_expr_nondett{int_type(), location}});
    exprt capacity = length;
    if(is_go_slice(type))
    {
      // Go1.27 §Length_and_capacity: "0 <= len(s) <= cap(s)". A nil slice
      // is possible: its data pointer is then null.
      const symbol_exprt cap = new_temporary(int_type(), location, dest);
      dest.add(code_frontend_assignt{
        cap, side_effect_expr_nondett{int_type(), location}});
      dest.add(code_assumet{and_exprt{
        binary_relation_exprt{length, ID_ge, from_integer(0, int_type())},
        binary_relation_exprt{length, ID_le, cap}}});
      capacity = cap;
    }
    else
    {
      dest.add(code_assumet{
        binary_relation_exprt{length, ID_ge, from_integer(0, int_type())}});
    }
    const exprt data =
      allocate_elements(element, capacity, false, location, dest);
    exprt::operandst operands{data, length};
    if(is_go_slice(type))
    {
      const symbol_exprt is_nil = new_temporary(bool_typet{}, location, dest);
      dest.add(code_frontend_assignt{
        is_nil, side_effect_expr_nondett{bool_typet{}, location}});
      dest.add(code_assumet{or_exprt{
        not_exprt{is_nil},
        equal_exprt{capacity, from_integer(0, int_type())}}});
      operands[0] = if_exprt{
        is_nil, null_pointer_exprt{to_pointer_type(data.type())}, data};
      operands.push_back(capacity);
    }
    return struct_exprt{std::move(operands), type};
  }
  const typet &followed =
    type.id() == ID_struct_tag ? ns.follow_tag(to_struct_tag_type(type)) : type;
  if(followed.id() == ID_struct)
  {
    exprt::operandst operands;
    bool has_composite = references;
    for(const auto &c : to_struct_type(followed).components())
      has_composite |= is_go_string(c.type()) || is_go_slice(c.type());
    if(has_composite)
    {
      // Field by field (the same depth: fields are part of the value).
      for(const auto &c : to_struct_type(followed).components())
      {
        exprt value = nondet_value(c.type(), location, dest, depth);
        if(value.is_nil())
          return nil_exprt{};
        operands.push_back(evaluate_once(value, location, dest));
      }
      return struct_exprt{std::move(operands), type};
    }
  }
  if(followed.id() == ID_array && references)
  {
    // Element by element.
    const array_typet &array = to_array_type(followed);
    const auto size = numeric_cast<std::size_t>(array.size());
    if(!size.has_value() || *size > 256)
    {
      unsupported(
        location,
        "cprover.Nondet and --function arguments of arrays of more than 256 "
        "elements with references");
      return nil_exprt{};
    }
    exprt::operandst elements;
    for(std::size_t i = 0; i < *size; ++i)
    {
      exprt value = nondet_value(array.element_type(), location, dest, depth);
      if(value.is_nil())
        return nil_exprt{};
      elements.push_back(evaluate_once(value, location, dest));
    }
    return array_exprt{std::move(elements), array};
  }
  return side_effect_expr_nondett{type, location};
}

exprt go_convertert::nondet_interface(
  const typet &type,
  const source_locationt &location,
  std::size_t depth)
{
  const std::size_t tid = go_type_id(type);
  irep_idt &id = nondet_interfaces[{tid, depth}];
  if(id.empty())
  {
    id = "go::$nondet." + std::to_string(tid) + "." + std::to_string(depth);
    declare_generated(symbol_table, id, {}, type);
  }
  const symbolt &function = ns.lookup(id);
  return side_effect_expr_function_callt{
    function.symbol_expr(),
    {},
    to_code_type(function.type).return_type(),
    location};
}

bool go_convertert::nondet_supported(
  const typet &type,
  std::set<irep_idt> &visited) const
{
  if(is_func_value(type) || is_go_chan(type))
    return false;
  if(
    type.id() == ID_pointer &&
    to_pointer_type(type).base_type().id() == ID_empty)
  {
    return false; // unsafe.Pointer
  }
  if(is_go_interface(type) || is_go_string(type))
    return true;
  if(is_go_slice(type))
  {
    return nondet_supported(
      to_pointer_type(to_struct_type(type).get_component("data").type())
        .base_type(),
      visited);
  }
  if(is_go_map(type))
  {
    const struct_typet &header =
      to_struct_type(to_pointer_type(type).base_type());
    return nondet_supported(
             to_pointer_type(header.get_component("keys").type()).base_type(),
             visited) &&
           nondet_supported(
             to_pointer_type(header.get_component("vals").type()).base_type(),
             visited);
  }
  if(type.id() == ID_pointer)
    return nondet_supported(to_pointer_type(type).base_type(), visited);
  if(type.id() == ID_struct_tag)
  {
    const irep_idt &tag = to_struct_tag_type(type).get_identifier();
    if(!visited.insert(tag).second)
      return true;
    return nondet_supported(ns.follow_tag(to_struct_tag_type(type)), visited);
  }
  if(type.id() == ID_struct)
  {
    for(const auto &c : to_struct_type(type).components())
    {
      if(!nondet_supported(c.type(), visited))
        return false;
    }
    return true;
  }
  if(type.id() == ID_array)
    return nondet_supported(to_array_type(type).element_type(), visited);
  return true;
}

void go_convertert::finish_nondet_interfaces()
{
  // Go1.27 §Interface_types: "A variable of interface type can store a
  // value of any type that is in the type set of the interface." The
  // candidates are the program's types: those that have the interface's
  // methods (their method sets, from the helper), or for interfaces
  // without methods the types the program converts to interfaces. A value
  // of another type cannot be built.
  const auto method_set_entry =
    [&](const irept &entry, const irep_idt &name, std::size_t signature) -> bool
  {
    for(const auto &m : go_list(entry, "mset"))
    {
      if(
        m.get("name") == name &&
        type_converter.type_id(m.get("type")) == signature)
      {
        return true;
      }
    }
    return false;
  };
  std::set<irep_idt> done;
  for(bool more = true; more;)
  {
    more = false;
    const auto pending = nondet_interfaces;
    for(const auto &n : pending)
    {
      if(!done.insert(n.second).second)
        continue;
      more = true;
      const std::size_t tid = n.first.first;
      const std::size_t depth = n.first.second;
      const symbolt &function = ns.lookup(n.second);
      const typet iface_type = to_code_type(function.type).return_type();
      const irept &iface_entry =
        type_converter.underlying(type_converter.type_index(tid));
      const auto &methods = go_list(iface_entry, "methods");

      std::vector<std::size_t> candidates;
      if(depth < nondet_parameters.max_nondet_tree_depth)
      {
        if(methods.empty())
        {
          for(const auto &t : dynamic_types)
            candidates.push_back(t.first);
        }
        else
        {
          for(std::size_t i = 0; i < parse_tree.types.size(); ++i)
          {
            const irep_idt index = std::to_string(i);
            const std::size_t candidate = type_converter.type_id(index);
            if(type_converter.type_index(candidate) != index)
              continue; // not the canonical entry
            const irept &entry = parse_tree.type_entry(index);
            if(type_converter.underlying(index).get("k") == "interface")
              continue;
            bool all = true;
            for(const auto &m : methods)
            {
              all =
                all &&
                method_set_entry(
                  entry, m.get("name"), type_converter.type_id(m.get("type")));
            }
            if(all)
              candidates.push_back(candidate);
          }
        }
      }

      function_statet outer = std::move(state);
      state = function_statet{};
      state.function_id = n.second;
      code_blockt body;
      const source_locationt &location = function.location;
      const bool nil_allowed = depth >= nondet_parameters.min_null_tree_depth;
      std::vector<code_blockt> choices;
      for(const std::size_t candidate : candidates)
      {
        const typet type = type_converter(type_converter.type_index(candidate));
        std::set<irep_idt> visited;
        if(type.is_nil() || !nondet_supported(type, visited))
          continue;
        code_blockt choice;
        const exprt value = nondet_value(type, location, choice, depth + 1);
        if(value.is_nil())
          continue;
        choice.add(code_frontend_returnt{
          typecast_exprt::conditional_cast(box(value, location), iface_type)});
        choices.push_back(std::move(choice));
      }
      for(std::size_t i = 0; i < choices.size(); ++i)
      {
        if(i + 1 == choices.size() && !nil_allowed)
          body.add(std::move(choices[i]));
        else
        {
          body.add(code_ifthenelset{
            side_effect_expr_nondett{bool_typet{}, location},
            std::move(choices[i])});
        }
      }
      body.add(
        code_frontend_returnt{*zero_initializer(iface_type, location, ns)});
      end_generated_function(body, iface_type);
      state = std::move(outer);
      symbol_table.get_writeable_ref(n.second).value = std::move(body);
    }
  }
}

void go_convertert::convert_entry()
{
  // With --function, the program's entry function is called with arbitrary
  // arguments (see nondet_value), from go::$entry (go_language.cpp).
  if(entry_function.empty())
    return;
  const symbolt *function = symbol_table.lookup(
    symbol_id(id2string(parse_tree.package) + "." + entry_function));
  if(function == nullptr)
    return; // reported by go_languaget
  const code_typet &code = to_code_type(function->type);
  if(code.parameters().empty())
    return;
  state = function_statet{};
  state.function_id = "go::$entry";
  const source_locationt &location = function->location;
  code_blockt body;
  exprt::operandst arguments;
  for(const auto &parameter : code.parameters())
  {
    std::set<irep_idt> visited;
    if(!nondet_supported(parameter.type(), visited))
    {
      unsupported(
        location,
        "--function with parameters containing functions or channels");
      return;
    }
    const exprt value = nondet_value(parameter.type(), location, body);
    if(value.is_nil())
      return;
    arguments.push_back(evaluate_once(value, location, body));
  }
  body.add(code_expressiont{side_effect_expr_function_callt{
    function->symbol_expr(),
    std::move(arguments),
    code.return_type(),
    location}});
  end_generated_function(body, empty_typet{});
  declare_generated(symbol_table, "go::$entry", {}, empty_typet{});
  symbol_table.get_writeable_ref("go::$entry").value = std::move(body);
}

exprt go_convertert::element(
  const exprt &value,
  const exprt &index,
  bool constant_index,
  const source_locationt &location,
  code_blockt &checks)
{
  // Go1.27 §Index_expressions: "the index x is in range if 0 <= x < len(a),
  // otherwise it is out of range"; for slices and strings, "if x is out of
  // range at run time, a run-time panic occurs". A constant index into a
  // constant string is checked by go/types; slices and other strings need
  // the check.
  (void)constant_index;
  const signedbv_typet wide{65};
  const typecast_exprt i{index, wide};
  add_panic(
    checks,
    and_exprt{
      binary_relation_exprt{i, ID_ge, from_integer(0, wide)},
      binary_relation_exprt{
        i, ID_lt, typecast_exprt{component(value, "len"), wide}}},
    "index-out-of-range",
    "runtime error: index out of range",
    location);
  dereference_exprt result{plus_exprt{
    component(value, "data"),
    typecast_exprt::conditional_cast(index, int_type())}};
  result.add_source_location() = location;
  return std::move(result);
}

exprt go_convertert::convert_slice_expr(const irept &expr)
{
  // Go1.27 §Slice_expressions. "For arrays or strings, the indices are in
  // range if 0 <= low <= high <= len(a), otherwise they are out of range.
  // For slices, the upper index bound is the slice capacity cap(a) rather
  // than the length." For full slice expressions, "The indices are in range
  // if 0 <= low <= high <= max <= cap(a)". "If the indices are out of range
  // at run time, a run-time panic occurs." "If the sliced operand of a
  // valid slice expression is a nil slice, the result is a nil slice.
  // Otherwise, if the result is a slice, it shares its underlying array
  // with the operand."
  const source_locationt location = location_of(expr);
  const irept &x = go_field(expr, "X");
  const irept &under = type_converter.underlying(x.get("_t"));
  const typet result_type = expr_type(expr);
  if(result_type.is_nil())
    return nil_exprt{};

  access_partst parts;
  exprt data, length, capacity;
  if(under.get("k") == "array" || under.get("k") == "pointer")
  {
    // "If the sliced operand is an array, it must be addressable"; it lives
    // on the heap or is a package-level variable (scan_heap_variables).
    exprt array;
    if(under.get("k") == "array")
      array = convert_access(x, parts);
    else
    {
      // "If a is a pointer to an array, a[low : high] is shorthand for
      // (*a)[low : high]."
      exprt pointer = access_operand(x, parts);
      if(pointer.is_nil())
        return nil_exprt{};
      pointer.add_source_location() = location;
      check_not_nil(pointer, parts.checks);
      array = dereference_exprt{pointer};
    }
    if(array.is_nil())
      return nil_exprt{};
    data = address_of_exprt{
      index_exprt{array, from_integer(0, c_index_type())},
      pointer_typet{
        to_array_type(array.type()).element_type(),
        config.ansi_c.pointer_width}};
    length = typecast_exprt::conditional_cast(
      to_array_type(array.type()).size(), int_type());
    capacity = length;
  }
  else
  {
    exprt value = access_operand(x, parts);
    if(value.is_nil())
      return nil_exprt{};
    if(!value.is_constant() && value.id() != ID_symbol)
      value = access_operand(value, parts); // evaluate once
    data = component(value, "data");
    length = component(value, "len");
    capacity =
      is_go_slice(value.type()) ? exprt{component(value, "cap")} : length;
  }

  const auto index = [&](const char *field, const exprt &absent) -> exprt
  {
    const irept &node = go_field(expr, field);
    if(node.is_nil())
      return absent;
    exprt value = access_operand(node, parts);
    if(value.is_nil())
      return nil_exprt{};
    return typecast_exprt::conditional_cast(value, int_type());
  };
  const exprt low = index("Low", from_integer(0, int_type()));
  const exprt high = index("High", length);
  const bool full = expr.get_bool("Slice3");
  const exprt max = full ? index("Max", capacity) : capacity;
  if(low.is_nil() || high.is_nil() || max.is_nil())
    return nil_exprt{};

  exprt::operandst in_range{
    binary_relation_exprt{from_integer(0, int_type()), ID_le, low},
    binary_relation_exprt{low, ID_le, high}};
  if(is_go_string(result_type))
    in_range.push_back(binary_relation_exprt{high, ID_le, length});
  else
  {
    in_range.push_back(binary_relation_exprt{high, ID_le, max});
    in_range.push_back(binary_relation_exprt{max, ID_le, capacity});
  }
  add_panic(
    parts.checks,
    conjunction(in_range),
    "slice-bounds-out-of-range",
    "runtime error: slice bounds out of range",
    location);

  exprt::operandst operands{plus_exprt{data, low}, minus_exprt{high, low}};
  if(is_go_slice(result_type))
    operands.push_back(minus_exprt{max, low});
  return with_parts(
    at(struct_exprt{std::move(operands), result_type}, location), parts);
}

exprt go_convertert::convert_make(const irept &call)
{
  // Go1.27 §Making_slices_maps_and_channels: make(T, n) is a slice of type T
  // with length n and capacity n, make(T, n, m) one with length n and
  // capacity m. "For slices and channels, if n is negative or larger
  // than m at run time, a run-time panic occurs." "A slice created with
  // make always allocates a new, hidden array".
  const source_locationt location = location_of(call);
  const typet type = expr_type(call);
  if(type.is_nil())
    return nil_exprt{};
  const auto &args = go_list(call, "Args");
  code_blockt block;
  if(is_go_map(type))
  {
    // make(T, n) for a map type T is a "map of type T with initial space for
    // approximately n elements"; the hint does not change the map's
    // behaviour, but it is evaluated.
    if(args.size() > 1)
    {
      exprt hint = convert_expr(args[1]);
      if(hint.is_nil())
        return nil_exprt{};
      block.add(expression_statement(std::move(hint), location));
    }
    const exprt map = make_map(type, location, block);
    return with_prefix(map, block, location);
  }
  if(is_go_chan(type))
  {
    // "make(T)" or "make(T, n)": an unbuffered or buffered channel.
    exprt size = from_integer(0, signedbv_typet{config.go.int_width});
    if(args.size() > 1)
    {
      size = convert_expr(args[1]);
      if(size.is_nil())
        return nil_exprt{};
    }
    const exprt chan = make_chan(type, size, location, block);
    return with_prefix(chan, block, location);
  }
  if(!is_go_slice(type))
  {
    unsupported(call, "make of type " + type.get_string(ID_C_go_type));
    return nil_exprt{};
  }
  const auto size = [&](const irept &node) -> exprt
  {
    exprt value = convert_expr(node);
    if(value.is_nil())
      return value;
    // Compare in a type that holds any integer argument.
    const symbol_exprt tmp = new_temporary(signedbv_typet{65}, location, block);
    block.add(code_frontend_assignt{
      tmp, typecast_exprt::conditional_cast(value, signedbv_typet{65})});
    return std::move(tmp);
  };
  const exprt n = size(args[1]);
  const exprt m = args.size() > 2 ? size(args[2]) : n;
  if(n.is_nil() || m.is_nil())
    return nil_exprt{};
  // "A constant size argument must be non-negative and representable by a
  // value of type int"; "If both n and m are provided and are constant,
  // then n must be no larger than m." go/types checks these.
  const bool all_constant = std::all_of(
    args.begin() + 1,
    args.end(),
    [](const irept &arg) { return !arg.get("_cv").empty(); });
  const signedbv_typet wide{65};
  if(!all_constant)
  {
    add_panic(
      block,
      and_exprt{
        binary_relation_exprt{n, ID_ge, from_integer(0, wide)},
        binary_relation_exprt{n, ID_le, m},
        binary_relation_exprt{m, ID_le, from_integer(max_int(), wide)}},
      "makeslice",
      "runtime error: makeslice: len out of range",
      location);
  }
  const typet &element =
    to_pointer_type(to_struct_type(type).get_component("data").type())
      .base_type();
  const exprt capacity = typecast_exprt{m, int_type()};
  const exprt data =
    allocate_elements(element, capacity, true, location, block);
  return with_prefix(
    struct_exprt{{data, typecast_exprt{n, int_type()}, capacity}, type},
    block,
    location);
}

/// \p pointer moved by \p offset bytes (a value of any integer type), with
/// the type of \p pointer.
static exprt byte_offset(const exprt &pointer, const exprt &offset)
{
  const pointer_typet bytes{unsigned_char_type(), config.ansi_c.pointer_width};
  return typecast_exprt{
    plus_exprt{
      typecast_exprt::conditional_cast(pointer, bytes),
      typecast_exprt::conditional_cast(
        offset, signedbv_typet{config.ansi_c.pointer_width})},
    pointer.type()};
}

exprt go_convertert::convert_unsafe_call(
  const irept &call,
  const irep_idt &name)
{
  const source_locationt location = location_of(call);
  const auto &args = go_list(call, "Args");
  const typet type = expr_type(call);
  if(type.is_nil())
    return nil_exprt{};
  code_blockt block;
  exprt::operandst values;
  for(const auto &arg : args)
  {
    exprt value = convert_expr(arg);
    if(value.is_nil())
      return nil_exprt{};
    values.push_back(evaluate_once(value, location, block));
  }

  if(name == "Add")
  {
    // "The function Add adds len to ptr and returns the updated pointer
    // unsafe.Pointer(uintptr(ptr) + uintptr(len))"
    return with_prefix(byte_offset(values[0], values[1]), block, location);
  }
  if(name == "SliceData" || name == "StringData")
  {
    // "The function SliceData returns a pointer to the underlying array of
    // the slice argument." "If slice is nil, the result is nil." "The
    // function StringData returns a pointer to the underlying bytes of the
    // str argument. For an empty string the return value is unspecified,
    // and may be nil."
    return with_prefix(
      typecast_exprt::conditional_cast(component(values[0], "data"), type),
      block,
      location);
  }
  if(name != "Slice" && name != "String")
  {
    unsupported(call, "unsafe." + id2string(name));
    return nil_exprt{};
  }

  // "The function Slice returns a slice whose underlying array starts at
  // ptr and whose length and capacity are len." "The function String
  // returns a string value whose underlying bytes start at ptr and whose
  // length is len." "At run time, if len is negative, or if ptr is nil and
  // len is not zero, a run-time panic occurs" (for String too: "The same
  // requirements apply to the ptr and len argument as in the function
  // Slice."). The messages are gc's; gc
  // also panics if the memory would extend past the end of the address
  // space, which only a length whose size overflows uintptr is here.
  const exprt &ptr = values[0];
  const signedbv_typet wide{65};
  const exprt len = typecast_exprt::conditional_cast(values[1], wide);
  const std::string what = "unsafe." + id2string(name);
  const exprt zero = from_integer(0, wide);
  add_panic(
    block,
    binary_relation_exprt{len, ID_ge, zero},
    "unsafe-" + id2string(name),
    "runtime error: " + what + ": len out of range",
    location);
  add_panic(
    block,
    or_exprt{
      notequal_exprt{ptr, null_pointer_exprt{to_pointer_type(ptr.type())}},
      equal_exprt{len, zero}},
    "unsafe-" + id2string(name),
    "runtime error: " + what + ": ptr is nil and len is not zero",
    location);
  const typet &element = to_pointer_type(ptr.type()).base_type();
  const auto element_size = pointer_offset_size(element, ns);
  if(element_size.has_value() && *element_size > 0)
  {
    const mp_integer limit =
      (power(2, config.ansi_c.pointer_width) - 1) / *element_size;
    add_panic(
      block,
      binary_relation_exprt{len, ID_le, from_integer(limit, wide)},
      "unsafe-" + id2string(name),
      "runtime error: " + what + ": len out of range",
      location);
  }
  // Also lengths of other integer types than int are representable then
  // (except on 32-bit targets, where gc checks int64 lengths as above).
  const exprt length = typecast_exprt{len, int_type()};
  const struct_typet &result = to_struct_type(type);
  const typet &data_type = result.get_component("data").type();
  exprt::operandst components{
    typecast_exprt::conditional_cast(ptr, data_type), length};
  if(name == "Slice")
    components.push_back(length);
  return with_prefix(struct_exprt{components, type}, block, location);
}

exprt go_convertert::convert_append(const irept &call)
{
  // Go1.27 §Appending_and_copying_slices: "If the capacity of s is not
  // large enough to fit the additional values, append allocates a new,
  // sufficiently large underlying array that fits both the existing slice
  // elements and the additional values. Otherwise, append re-uses the
  // underlying array." The new capacity is not specified
  // (append_capacity_choice). "the result is independent of
  // whether the memory referenced by the arguments overlaps."
  const source_locationt location = location_of(call);
  const typet type = expr_type(call);
  if(type.is_nil())
    return nil_exprt{};
  const auto &args = go_list(call, "Args");
  code_blockt block;
  exprt s = convert_expr(args.front());
  if(s.is_nil())
    return nil_exprt{};
  s = assign_cast(s, type);
  const symbol_exprt slice = new_temporary(type, location, block);
  block.add(code_frontend_assignt{slice, s});
  const typet &element =
    to_pointer_type(to_struct_type(type).get_component("data").type())
      .base_type();

  // The values appended: a slice (or string) with "...", or the remaining
  // arguments, evaluated in order into a new array.
  exprt extra_data, extra_len;
  if(call.get_bool("_ellipsis"))
  {
    exprt t = convert_expr(args[1]);
    if(t.is_nil())
      return nil_exprt{};
    t = assign_cast(t, type); // nil or a string with []byte
    const symbol_exprt tail = new_temporary(t.type(), location, block);
    block.add(code_frontend_assignt{tail, t});
    extra_data = component(tail, "data");
    extra_len = component(tail, "len");
  }
  else
  {
    const std::size_t count = args.size() - 1;
    extra_len = from_integer(count, int_type());
    if(count > 0)
    {
      extra_data =
        allocate_elements(element, extra_len, false, location, block);
      const symbol_exprt values =
        new_temporary(extra_data.type(), location, block);
      block.add(code_frontend_assignt{values, extra_data});
      for(std::size_t i = 0; i < count; ++i)
      {
        exprt value = convert_expr(args[i + 1]);
        if(value.is_nil())
          return nil_exprt{};
        block.add(code_frontend_assignt{
          dereference_exprt{plus_exprt{values, from_integer(i, int_type())}},
          assign_cast(value, element)});
      }
      extra_data = values;
    }
    else
      return with_prefix(std::move(slice), block, location);
  }

  // The new length; growing beyond what int holds panics.
  const symbol_exprt length = new_temporary(int_type(), location, block);
  const signedbv_typet wide{65};
  add_panic(
    block,
    binary_relation_exprt{
      plus_exprt{
        typecast_exprt{component(slice, "len"), wide},
        typecast_exprt{extra_len, wide}},
      ID_le,
      from_integer(max_int(), wide)},
    "makeslice",
    "runtime error: growslice: len out of range",
    location);
  block.add(code_frontend_assignt{
    length, plus_exprt{component(slice, "len"), extra_len}});

  const symbol_exprt result = new_temporary(type, location, block);
  // Enough capacity: write into the same underlying array.
  code_blockt reuse;
  copy_elements(
    plus_exprt{component(slice, "data"), component(slice, "len")},
    extra_data,
    extra_len,
    location,
    reuse);
  reuse.add(code_frontend_assignt{
    result,
    struct_exprt{
      {component(slice, "data"), length, component(slice, "cap")}, type}});
  // Otherwise: a new array, of a capacity that append_capacity defines.
  code_blockt grow;
  const symbol_exprt capacity = new_temporary(int_type(), location, grow);
  const exprt count = append_capacity_choice(
    type_converter.underlying(call.get("_t")),
    capacity,
    length,
    component(slice, "len"),
    component(slice, "cap"),
    location,
    grow);
  const exprt fresh = allocate_elements(element, count, true, location, grow);
  const symbol_exprt data = new_temporary(fresh.type(), location, grow);
  grow.add(code_frontend_assignt{data, fresh});
  copy_elements(
    data, component(slice, "data"), component(slice, "len"), location, grow);
  copy_elements(
    plus_exprt{data, component(slice, "len")},
    extra_data,
    extra_len,
    location,
    grow);
  grow.add(code_frontend_assignt{
    result, struct_exprt{{data, length, capacity}, type}});
  block.add(code_ifthenelset{
    binary_relation_exprt{length, ID_le, component(slice, "cap")},
    std::move(reuse),
    std::move(grow)});
  return with_prefix(std::move(result), block, location);
}

exprt go_convertert::append_capacity_choice(
  const irept &slice_type,
  const symbol_exprt &capacity,
  const exprt &length,
  const exprt &old_len,
  const exprt &old_cap,
  const source_locationt &location,
  code_blockt &grow)
{
  if(append_capacity == go_append_capacityt::TINYGO)
  {
    // The smallest power of two above the length, for all element types.
    grow.add(code_frontend_assignt{
      capacity, runtime_call("GrowCapTinyGo", {length}, location)});
    return capacity;
  }

  const irep_idt &size_value = slice_type.get("esize");
  if(append_capacity == go_append_capacityt::ANY || size_value.empty())
  {
    // Any capacity that fits the elements.
    grow.add(code_frontend_assignt{
      capacity, side_effect_expr_nondett{int_type(), location}});
    grow.add(code_assumet{binary_relation_exprt{capacity, ID_ge, length}});
    return capacity;
  }

  // gc go1.27: the capacity runtime.growslice chooses for elements of
  // this size, unless the slice is backed by a stack buffer (which
  // depends on gc's escape analysis): StackCapGC elements on the first
  // append to an empty slice that fits, or RoundUpCapGC(length) when the
  // slice fits. Both are at most StackCapGC, so that allocating
  // max(growslice's, StackCapGC) elements gives a constant size whenever
  // the lengths are constant.
  const mp_integer element_size = string2integer(id2string(size_value));
  const exprt size = from_integer(element_size, int_type());
  const exprt pointers =
    slice_type.get_bool("eptrs") ? exprt{true_exprt{}} : exprt{false_exprt{}};
  const symbol_exprt grown = new_temporary(int_type(), location, grow);
  grow.add(code_frontend_assignt{
    grown,
    runtime_call("GrowCapGC", {length, old_cap, size, pointers}, location)});
  grow.add(code_frontend_assignt{capacity, grown});

  const mp_integer stack_buffer_size = 32; // rt.stackBufferSize
  if(element_size == 0 || element_size > stack_buffer_size)
    return grown;
  // In the models of the standard library (and the runtime), slices are
  // taken to be on the heap, as those that outlive their function are: the
  // capacities are then determined, rather than depending on escape
  // analysis, which keeps the arrays of the models' buffers (e.g., of
  // encoding/json) unique. The capacities of their internal slices are not
  // observable.
  const std::string &function = id2string(state.function_id);
  for(const char *prefix :
      {"go::cprover/", "go::reflect.", "go::sync.", "go::sync/atomic."})
  {
    if(function.compare(0, std::strlen(prefix), prefix) == 0)
      return grown;
  }
  const exprt stack_cap =
    from_integer(stack_buffer_size / element_size, int_type());
  const binary_relation_exprt fits{length, ID_le, stack_cap};

  code_ifthenelset whole_buffer{
    and_exprt{
      fits,
      equal_exprt{old_len, from_integer(0, old_len.type())},
      side_effect_expr_nondett{bool_typet{}, location}},
    code_frontend_assignt{capacity, stack_cap}};
  whole_buffer.add_source_location() = location;
  grow.add(std::move(whole_buffer));
  code_ifthenelset rounded_up{
    and_exprt{fits, side_effect_expr_nondett{bool_typet{}, location}},
    code_frontend_assignt{
      capacity,
      runtime_call("RoundUpCapGC", {length, size, pointers}, location)}};
  rounded_up.add_source_location() = location;
  grow.add(std::move(rounded_up));

  return if_exprt{
    and_exprt{fits, binary_relation_exprt{grown, ID_lt, stack_cap}},
    stack_cap,
    grown};
}

exprt go_convertert::convert_copy(const irept &call)
{
  // Go1.27 §Appending_and_copying_slices: "The function copy copies slice
  // elements from a source src to a destination dst and returns the number
  // of elements copied." "The number of elements copied is the minimum of
  // len(src) and len(dst)." "As a special case, copy also accepts a
  // destination argument assignable to type []byte with a source argument
  // of a string type."
  const source_locationt location = location_of(call);
  const auto &args = go_list(call, "Args");
  code_blockt block;
  exprt dst = convert_expr(args[0]);
  exprt src = convert_expr(args[1]);
  if(dst.is_nil() || src.is_nil())
    return nil_exprt{};
  const symbol_exprt d = new_temporary(dst.type(), location, block);
  block.add(code_frontend_assignt{d, dst});
  const symbol_exprt s = new_temporary(src.type(), location, block);
  block.add(code_frontend_assignt{s, src});
  const symbol_exprt n = new_temporary(int_type(), location, block);
  block.add(code_frontend_assignt{
    n,
    if_exprt{
      binary_relation_exprt{component(d, "len"), ID_le, component(s, "len")},
      component(d, "len"),
      component(s, "len")}});
  copy_elements(component(d, "data"), component(s, "data"), n, location, block);
  return with_prefix(std::move(n), block, location);
}

void go_convertert::convert_clear(const irept &call, code_blockt &dest)
{
  // Go1.27 §Clear: "clear(s) []T sets all elements up to the length of s to
  // the zero value of T". "If the map or slice is nil, clear is a no-op."
  const source_locationt location = location_of(call);
  exprt s = convert_expr(go_list(call, "Args").front());
  if(s.is_nil())
    return;
  if(is_go_map(s.type()))
  {
    // "clear(m) map[K]T deletes all entries, resulting in an empty map
    // (len(m) == 0)".
    const exprt map = evaluate_once(s, location, dest);
    const struct_typet &header =
      to_struct_type(to_pointer_type(map.type()).base_type());
    dest.add(code_ifthenelset{
      notequal_exprt{map, null_pointer_exprt{to_pointer_type(map.type())}},
      code_frontend_assignt{
        member_exprt{dereference_exprt{map}, header.get_component("len")},
        from_integer(0, int_type())}});
    return;
  }
  if(!is_go_slice(s.type()))
    return unsupported(
      call, "clear of type " + s.type().get_string(ID_C_go_type));
  const symbol_exprt slice = new_temporary(s.type(), location, dest);
  dest.add(code_frontend_assignt{slice, s});
  const typet element =
    to_pointer_type(component(slice, "data").type()).base_type();
  code_blockt zero;
  const exprt zeros = evaluate_once(
    allocate_elements(element, component(slice, "len"), true, location, zero),
    location,
    zero);
  copy_elements(
    component(slice, "data"), zeros, component(slice, "len"), location, zero);
  dest.add(code_ifthenelset{
    binary_relation_exprt{
      component(slice, "len"), ID_gt, from_integer(0, int_type())},
    std::move(zero)});
}

exprt go_convertert::convert_constant_array_lit(
  const irept &lit,
  const typet &type)
{
  // Generated tables (e.g., of package unicode or x/text) are literals of
  // thousands of constants: the helper sends their values, which form one
  // array constant instead of an assignment per element.
  const source_locationt location = location_of(lit);
  const bool is_slice = is_go_slice(type);
  const typet &element =
    is_slice
      ? to_pointer_type(to_struct_type(type).get_component("data").type())
          .base_type()
      : to_array_type(type).element_type();
  const auto &values = go_list(lit, "_cvals");
  // A value: a number ("b" and the IEEE 754 bits for floating-point
  // types), a Boolean, or the list of the elements or fields of an array
  // or a struct (see the helper's constantArray).
  std::function<exprt(const irept &, const typet &)> value_of =
    [&](const irept &value, const typet &type) -> exprt
  {
    const typet &followed = go_follow(ns, type);
    if(followed.id() == ID_struct || followed.id() == ID_array)
    {
      const auto &items = value.get_sub();
      exprt::operandst operands;
      if(followed.id() == ID_struct)
      {
        const auto &components = to_struct_type(followed).components();
        for(std::size_t i = 0; i < components.size(); ++i)
          operands.push_back(value_of(items.at(i), components[i].type()));
        return struct_exprt{std::move(operands), type};
      }
      const typet &item_type = to_array_type(followed).element_type();
      for(const auto &item : items)
        operands.push_back(value_of(item, item_type));
      return array_exprt{std::move(operands), to_array_type(type)};
    }
    const std::string &text = value.id_string();
    if(text == "true" || text == "false")
      return from_integer(text == "true" ? 1 : 0, type);
    if(!text.empty() && text[0] == 'b')
    {
      return constant_exprt{
        integer2bvrep(
          string2integer(text.substr(1)), to_floatbv_type(type).get_width()),
        type};
    }
    return from_integer(string2integer(text), type);
  };
  exprt::operandst elements;
  elements.reserve(values.size());
  for(const auto &value : values)
    elements.push_back(value_of(value, element));
  const array_typet array_type{
    element, from_integer(values.size(), c_index_type())};
  array_exprt array{std::move(elements), array_type};
  array.add_source_location() = location;
  if(!is_slice)
    return typecast_exprt::conditional_cast(std::move(array), type);
  // "A slice literal describes the entire underlying array literal": a new
  // array, with the literal's values.
  code_blockt block;
  const symbol_exprt pointer =
    new_heap_variable(array_type, array, location, block);
  const exprt data = address_of_exprt{
    index_exprt{dereference_exprt{pointer}, from_integer(0, c_index_type())},
    to_pointer_type(to_struct_type(type).get_component("data").type())};
  const exprt length = from_integer(values.size(), int_type());
  return with_prefix(
    struct_exprt{{data, length, length}, type}, block, location);
}

exprt go_convertert::convert_slice_lit(const irept &lit, const typet &type)
{
  // Go1.27 §Composite_literals: "A slice literal describes the entire
  // underlying array literal. Thus the length and capacity of a slice
  // literal are the maximum element index plus one." The elements of the
  // new underlying array are evaluated in order; missing ones are zero.
  const source_locationt location = location_of(lit);
  const typet &element =
    to_pointer_type(to_struct_type(type).get_component("data").type())
      .base_type();
  // The length: the maximum index plus one.
  mp_integer next = 0, length = 0;
  for(const auto &e : go_list(lit, "Elts"))
  {
    if(e.id() == "KeyValueExpr")
      next = string2integer(go_field(e, "Key").get_string("_cv"));
    ++next;
    length = std::max(length, next);
  }
  code_blockt block;
  const exprt fresh = allocate_elements(
    element, from_integer(length, int_type()), true, location, block);
  const symbol_exprt data = new_temporary(fresh.type(), location, block);
  block.add(code_frontend_assignt{data, fresh});
  next = 0;
  for(const auto &e : go_list(lit, "Elts"))
  {
    const bool keyed = e.id() == "KeyValueExpr";
    if(keyed)
      next = string2integer(go_field(e, "Key").get_string("_cv"));
    const irept &value_node = keyed ? go_field(e, "Value") : e;
    exprt value = convert_expr(value_node);
    if(value.is_nil())
      return nil_exprt{};
    code_frontend_assignt assign{
      dereference_exprt{plus_exprt{data, from_integer(next, int_type())}},
      assign_cast(value, element)};
    assign.add_source_location() = location_of(value_node);
    block.add(std::move(assign));
    ++next;
  }
  const exprt n = from_integer(length, int_type());
  return with_prefix(struct_exprt{{data, n, n}, type}, block, location);
}

exprt go_convertert::convert_string_conversion(
  const exprt &value,
  const typet &type,
  const irept &where)
{
  // Go1.27 §Conversions_to_and_from_a_string_type.
  const source_locationt location = location_of(where);
  const typet &from = value.type();
  const auto byte_like = [](const typet &t)
  { return t.id() == ID_unsignedbv && to_unsignedbv_type(t).get_width() == 8; };
  const auto rune_like = [](const typet &t)
  { return t.id() == ID_signedbv && to_signedbv_type(t).get_width() == 32; };
  const auto element_of = [](const typet &t) -> const typet &
  {
    return to_pointer_type(to_struct_type(t).get_component("data").type())
      .base_type();
  };

  if(is_go_string(type) && is_go_string(from))
  {
    // Between string types: the same bytes, of the new type.
    exprt result = value;
    result.type() = type;
    return result;
  }
  if(
    is_go_string(type) &&
    (from.id() == ID_signedbv || from.id() == ID_unsignedbv))
  {
    // "an integer value may be converted to a string type. This form of
    // conversion yields a string containing the (possibly multi-byte) UTF-8
    // representation of the Unicode code point with the given integer
    // value. Values outside the range of valid Unicode code points are
    // converted to "\uFFFD"." rt.EncodeRune handles invalid runes; values
    // that do not fit in a rune become -1, which is invalid.
    const signedbv_typet wide{65};
    code_blockt block;
    const exprt x = evaluate_once(value, location, block);
    const exprt rune = if_exprt{
      and_exprt{
        binary_relation_exprt{
          typecast_exprt{x, wide}, ID_ge, from_integer(0, wide)},
        binary_relation_exprt{
          typecast_exprt{x, wide}, ID_le, from_integer(0x10FFFF, wide)}},
      typecast_exprt{x, signedbv_typet{32}},
      from_integer(-1, signedbv_typet{32})};
    return with_prefix(
      runtime_call("EncodeRune", {rune}, location), block, location);
  }
  if(is_go_string(type) && is_go_slice(from) && byte_like(element_of(from)))
  {
    // "Converting a slice of bytes to a string type yields a string whose
    // successive bytes are the elements of the slice." Strings are
    // immutable, so the bytes are copied.
    code_blockt block;
    const exprt s = evaluate_once(value, location, block);
    const exprt fresh = allocate_elements(
      unsignedbv_typet{8}, component(s, "len"), false, location, block);
    const symbol_exprt data = new_temporary(fresh.type(), location, block);
    block.add(code_frontend_assignt{data, fresh});
    copy_elements(
      data,
      typecast_exprt::conditional_cast(component(s, "data"), data.type()),
      component(s, "len"),
      location,
      block);
    return with_prefix(
      struct_exprt{{data, component(s, "len")}, type}, block, location);
  }
  if(is_go_slice(type) && is_go_string(from) && byte_like(element_of(type)))
  {
    // "Converting a value of a string type to a slice of bytes type yields a
    // non-nil slice whose successive elements are the bytes of the string.
    // The capacity of the resulting slice is implementation-specific and may
    // be larger than the slice length." Here it is the length.
    code_blockt block;
    const exprt s = evaluate_once(value, location, block);
    const exprt fresh = allocate_elements(
      element_of(type), component(s, "len"), false, location, block);
    const symbol_exprt data = new_temporary(fresh.type(), location, block);
    block.add(code_frontend_assignt{data, fresh});
    copy_elements(
      data,
      typecast_exprt::conditional_cast(component(s, "data"), data.type()),
      component(s, "len"),
      location,
      block);
    return with_prefix(
      struct_exprt{{data, component(s, "len"), component(s, "len")}, type},
      block,
      location);
  }
  if(is_go_string(type) && is_go_slice(from) && rune_like(element_of(from)))
  {
    // "Converting a slice of runes to a string type yields a string that is
    // the concatenation of the individual rune values converted to
    // strings."
    return runtime_call("RunesToString", {value}, location);
  }
  if(is_go_slice(type) && is_go_string(from) && rune_like(element_of(type)))
  {
    // "Converting a value of a string type to a slice of runes type yields a
    // slice containing the individual Unicode code points of the string."
    return typecast_exprt::conditional_cast(
      runtime_call("StringToRunes", {value}, location), type);
  }
  return nil_exprt{};
}
