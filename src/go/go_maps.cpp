/*******************************************************************\

Module: Go front-end: maps

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// Conversion of maps (Go1.27 §Map_types) and of the operations on them:
/// index expressions (§Index_expressions), assignments, delete and clear
/// (§Deletion_of_map_elements, §Clear), make
/// (§Making_slices_maps_and_channels), map literals (§Composite_literals)
/// and range (§For_range).
///
/// Representation: a map value is a pointer to a header `{len, cap, keys
/// *K, vals *V}` on the heap, so that map values refer to the same map
/// after assignment; nil is the null pointer. Entries are kept in the
/// arrays keys[0:len] and vals[0:len], in no particular order. Lookups,
/// assignments and deletions search the keys linearly using Go's ==, so
/// they need loop iterations up to the number of entries.

#include <util/arith_tools.h>
#include <util/c_types.h>
#include <util/config.h>
#include <util/expr_initializer.h>
#include <util/pointer_expr.h>
#include <util/std_code.h>

#include "go_convert.h"
#include "go_parse_tree.h"

static signedbv_typet int_type()
{
  return signedbv_typet{config.go.int_width};
}

/// The header of map \p map (a pointer) and its components.
static exprt header(const exprt &map)
{
  return dereference_exprt{map};
}

static member_exprt field(const exprt &map, const char *name)
{
  const struct_typet &type =
    to_struct_type(to_pointer_type(map.type()).base_type());
  return member_exprt{header(map), type.get_component(name)};
}

static typet key_type(const typet &map_type)
{
  const struct_typet &type =
    to_struct_type(to_pointer_type(map_type).base_type());
  return to_pointer_type(type.get_component("keys").type()).base_type();
}

static typet value_type(const typet &map_type)
{
  const struct_typet &type =
    to_struct_type(to_pointer_type(map_type).base_type());
  return to_pointer_type(type.get_component("vals").type()).base_type();
}

/// A loop `for i := 0; i < count; i++ { body }` with a new counter i.
static code_fort counting_loop(
  const symbol_exprt &i,
  const exprt &count,
  codet body,
  const source_locationt &location)
{
  code_blockt advance;
  advance.add(
    code_frontend_assignt{i, plus_exprt{i, from_integer(1, i.type())}});
  code_fort loop{
    code_frontend_assignt{i, from_integer(0, i.type())},
    binary_relation_exprt{i, ID_lt, count},
    side_effect_expr_statement_expressiont{
      std::move(advance), empty_typet{}, location},
    std::move(body)};
  loop.add_source_location() = location;
  return loop;
}

symbol_exprt go_convertert::map_find(
  const exprt &map,
  const exprt &key,
  const irept &where,
  code_blockt &dest)
{
  // The index of the entry with key \p key, or -1.
  const source_locationt location = location_of(where);
  if(is_go_interface(key.type()))
  {
    // Go1.27 §Map_types: "If the key type is an interface type, these
    // comparison operators must be defined for the dynamic key values;
    // failure will cause a run-time panic." gc hashes the key, also for
    // a nil or empty map. The message is gc's without the type.
    add_panic(
      dest,
      as_condition(runtime_call("Comparable", {key}, location)),
      "uncomparable",
      "runtime error: hash of unhashable type",
      location);
  }
  const symbol_exprt found = new_temporary(int_type(), location, dest);
  dest.add(code_frontend_assignt{found, from_integer(-1, int_type())});
  code_blockt search;
  // The search reads the header once, then the entries through a snapshot
  // of the keys pointer whose type is marked: the data-race check
  // (go_race_check.cpp) takes the header reads as the operation's access
  // of the map, as gc does, and ignores the reads of entries, which would
  // otherwise be instrumented in every unwinding of the search.
  typet keys_type = field(map, "keys").type();
  keys_type.set(ID_C_go_map_entries, true);
  const symbol_exprt keys = new_temporary(keys_type, location, search);
  search.add(code_frontend_assignt{keys, field(map, "keys")});
  const symbol_exprt count = new_temporary(int_type(), location, search);
  search.add(code_frontend_assignt{count, field(map, "len")});
  const symbol_exprt i = new_temporary(int_type(), location, dest);
  const exprt candidate = dereference_exprt{plus_exprt{keys, i}};
  exprt same = as_condition(equality(candidate, key, where));
  code_blockt hit;
  hit.add(code_frontend_assignt{found, i});
  hit.add(code_breakt{});
  search.add(counting_loop(
    i, count, code_ifthenelset{std::move(same), std::move(hit)}, location));
  dest.add(code_ifthenelset{
    notequal_exprt{map, null_pointer_exprt{to_pointer_type(map.type())}},
    std::move(search)});
  return found;
}

exprt go_convertert::convert_map_lookup(const irept &expr, bool comma_ok)
{
  // Go1.27 §Index_expressions: "if the map contains an entry with key x,
  // a[x] is the map element with key x and the type of a[x] is the element
  // type of M"; "if the map is nil or does not contain such an entry, a[x]
  // is the zero value for the element type of M". In the special form v, ok =
  // a[x], "The value of ok is true if the key x is present in the map, and
  // false otherwise."
  const source_locationt location = location_of(expr);
  code_blockt block;
  exprt map = convert_expr(go_field(expr, "X"));
  exprt key = convert_expr(go_field(expr, "Index"));
  if(map.is_nil() || key.is_nil())
    return nil_exprt{};
  map = evaluate_once(map, location, block);
  key = evaluate_once(assign_cast(key, key_type(map.type())), location, block);
  const symbol_exprt found = map_find(map, key, expr, block);
  const typet element = value_type(map.type());
  const binary_relation_exprt present{
    found, ID_ge, from_integer(0, int_type())};
  const if_exprt value{
    present,
    dereference_exprt{plus_exprt{field(map, "vals"), found}},
    *zero_initializer(element, location, ns)};
  if(!comma_ok)
    return with_prefix(value, block, location);
  const typet tuple = expr_type(expr);
  const auto &components = to_struct_type(tuple).components();
  return with_prefix(
    struct_exprt{
      {assign_cast(value, components[0].type()),
       typecast_exprt::conditional_cast(present, components[1].type())},
      tuple},
    block,
    location);
}

void go_convertert::map_insert(
  const exprt &map,
  const exprt &key,
  const exprt &value,
  const irept &where,
  code_blockt &dest)
{
  // Go1.27 §Index_expressions: "Assigning to an element of a nil map causes
  // a run-time panic." Otherwise the entry for key is updated or added.
  const source_locationt location = location_of(where);
  add_panic(
    dest,
    notequal_exprt{map, null_pointer_exprt{to_pointer_type(map.type())}},
    "nil-map",
    "assignment to entry in nil map",
    location);
  const symbol_exprt found = map_find(map, key, where, dest);
  const typet k = key_type(map.type());
  const typet v = value_type(map.type());

  code_blockt update;
  update.add(code_frontend_assignt{
    dereference_exprt{plus_exprt{field(map, "vals"), found}}, value});

  code_blockt add;
  // Grow the arrays if they are full.
  code_blockt grow;
  const symbol_exprt capacity = new_temporary(int_type(), location, grow);
  grow.add(code_frontend_assignt{
    capacity,
    plus_exprt{
      mult_exprt{field(map, "cap"), from_integer(2, int_type())},
      from_integer(1, int_type())}});
  const exprt keys = evaluate_once(
    allocate_elements(k, capacity, false, location, grow), location, grow);
  const exprt vals = evaluate_once(
    allocate_elements(v, capacity, false, location, grow), location, grow);
  copy_elements(keys, field(map, "keys"), field(map, "len"), location, grow);
  copy_elements(vals, field(map, "vals"), field(map, "len"), location, grow);
  grow.add(code_frontend_assignt{field(map, "keys"), keys});
  grow.add(code_frontend_assignt{field(map, "vals"), vals});
  grow.add(code_frontend_assignt{field(map, "cap"), capacity});
  add.add(code_ifthenelset{
    equal_exprt{field(map, "len"), field(map, "cap")}, std::move(grow)});
  add.add(code_frontend_assignt{
    dereference_exprt{plus_exprt{field(map, "keys"), field(map, "len")}}, key});
  add.add(code_frontend_assignt{
    dereference_exprt{plus_exprt{field(map, "vals"), field(map, "len")}},
    value});
  add.add(code_frontend_assignt{
    field(map, "len"),
    plus_exprt{field(map, "len"), from_integer(1, int_type())}});

  code_ifthenelset choose{
    binary_relation_exprt{found, ID_ge, from_integer(0, int_type())},
    std::move(update),
    std::move(add)};
  choose.add_source_location() = location;
  dest.add(std::move(choose));
}

exprt go_convertert::make_map(
  const typet &type,
  const source_locationt &location,
  code_blockt &dest)
{
  // An empty map: a new header with no entries.
  const typet &header_type = to_pointer_type(type).base_type();
  const symbol_exprt map =
    new_heap_variable(header_type, nil_exprt{}, location, dest);
  dest.add(code_frontend_assignt{
    field(map, "keys"),
    allocate_elements(
      key_type(type), from_integer(0, int_type()), false, location, dest)});
  dest.add(code_frontend_assignt{
    field(map, "vals"),
    allocate_elements(
      value_type(type), from_integer(0, int_type()), false, location, dest)});
  return map;
}

exprt go_convertert::convert_map_lit(const irept &lit, const typet &type)
{
  // Go1.27 §Composite_literals: "For map literals, each element must have a
  // key." The keys and values are evaluated in order and inserted.
  // "It is an error to specify multiple elements with the same field selector
  // or constant key value." So, as long as all keys so far are constants,
  // an element is a new entry, without searching the map (a loop that
  // would need unwinding). The arrays have room for all elements.
  const source_locationt location = location_of(lit);
  code_blockt block;
  const auto &elements = go_list(lit, "Elts");
  const exprt map = make_map(type, location, block);
  const exprt capacity = from_integer(elements.size(), int_type());
  block.add(code_frontend_assignt{
    field(map, "keys"),
    allocate_elements(key_type(type), capacity, false, location, block)});
  block.add(code_frontend_assignt{
    field(map, "vals"),
    allocate_elements(value_type(type), capacity, false, location, block)});
  block.add(code_frontend_assignt{field(map, "cap"), capacity});
  bool distinct = true;
  for(const auto &element : elements)
  {
    const irept &key_node = go_field(element, "Key");
    exprt key = convert_expr(key_node);
    exprt value = convert_expr(go_field(element, "Value"));
    if(key.is_nil() || value.is_nil())
      return nil_exprt{};
    key = evaluate_once(assign_cast(key, key_type(type)), location, block);
    value =
      evaluate_once(assign_cast(value, value_type(type)), location, block);
    distinct = distinct && !key_node.get("_cv").empty();
    if(!distinct)
    {
      map_insert(map, key, value, element, block);
      continue;
    }
    block.add(code_frontend_assignt{
      dereference_exprt{plus_exprt{field(map, "keys"), field(map, "len")}},
      key});
    block.add(code_frontend_assignt{
      dereference_exprt{plus_exprt{field(map, "vals"), field(map, "len")}},
      value});
    block.add(code_frontend_assignt{
      field(map, "len"),
      plus_exprt{field(map, "len"), from_integer(1, int_type())}});
  }
  return with_prefix(map, block, location);
}

void go_convertert::convert_delete(const irept &call, code_blockt &dest)
{
  // Go1.27 §Deletion_of_map_elements: "The built-in function delete removes
  // the element with key k from a map m." "If the map m is nil or the
  // element m[k] does not exist, delete is a no-op." The last entry takes
  // the place of the removed one.
  const source_locationt location = location_of(call);
  const auto &args = go_list(call, "Args");
  exprt map = convert_expr(args[0]);
  exprt key = convert_expr(args[1]);
  if(map.is_nil() || key.is_nil())
    return;
  map = evaluate_once(map, location, dest);
  key = evaluate_once(assign_cast(key, key_type(map.type())), location, dest);
  delete_entry(map, key, call, dest);
}

void go_convertert::delete_entry(
  const exprt &map,
  const exprt &key,
  const irept &where,
  code_blockt &dest)
{
  // gc's mapdelete writes the map also when there is no such entry.
  code_blockt write;
  race_marker("go::$race_write", map, location_of(where), write);
  dest.add(code_ifthenelset{
    notequal_exprt{map, null_pointer_exprt{to_pointer_type(map.type())}},
    std::move(write)});
  const symbol_exprt found = map_find(map, key, where, dest);
  code_blockt remove;
  const exprt last =
    minus_exprt{field(map, "len"), from_integer(1, int_type())};
  remove.add(code_frontend_assignt{
    dereference_exprt{plus_exprt{field(map, "keys"), found}},
    dereference_exprt{plus_exprt{field(map, "keys"), last}}});
  remove.add(code_frontend_assignt{
    dereference_exprt{plus_exprt{field(map, "vals"), found}},
    dereference_exprt{plus_exprt{field(map, "vals"), last}}});
  remove.add(code_frontend_assignt{field(map, "len"), last});
  dest.add(code_ifthenelset{
    binary_relation_exprt{found, ID_ge, from_integer(0, int_type())},
    std::move(remove)});
}

void go_convertert::convert_map_range(
  const irept &stmt,
  const exprt &map_value,
  code_blockt &dest,
  const irep_idt &label_object)
{
  // Go1.27 §For_range: "The iteration order over maps is not specified and
  // is not guaranteed to be the same from one iteration to the next. If a
  // map entry that has not yet been reached is removed during iteration,
  // the corresponding iteration value will not be produced. If a map entry
  // is created during iteration, that entry may be produced during the
  // iteration or may be skipped." "If the map is nil, the number of
  // iterations is 0." The keys present when the loop starts are visited
  // in a nondeterministic order (entries created later are skipped, as
  // permitted); keys deleted before they are reached are skipped, and the
  // value is the entry's current one.
  const source_locationt location = location_of(stmt);
  code_blockt scope;
  const symbol_exprt map = new_temporary(map_value.type(), location, scope);
  scope.add(code_frontend_assignt{map, map_value});
  const typet k = key_type(map.type());
  const null_pointer_exprt nil{to_pointer_type(map.type())};

  // A snapshot of the keys, and which of them have been visited.
  const symbol_exprt count = new_temporary(int_type(), location, scope);
  scope.add(code_frontend_assignt{
    count,
    if_exprt{
      equal_exprt{map, nil}, from_integer(0, int_type()), field(map, "len")}});
  // The arrays have the map's capacity, which unlike its length usually
  // does not depend on the path (arrays of symbolic size are expensive);
  // they are allocated where the map is not nil, where goto-symex knows
  // which header the map points to.
  const pointer_typet key_pointer{k, config.ansi_c.pointer_width};
  const pointer_typet byte_pointer{
    unsignedbv_typet{8}, config.ansi_c.pointer_width};
  const symbol_exprt keys = new_temporary(key_pointer, location, scope);
  const symbol_exprt visited = new_temporary(byte_pointer, location, scope);
  scope.add(code_frontend_assignt{keys, null_pointer_exprt{key_pointer}});
  scope.add(code_frontend_assignt{visited, null_pointer_exprt{byte_pointer}});
  code_blockt snapshot;
  snapshot.add(code_frontend_assignt{
    keys, allocate_elements(k, field(map, "cap"), false, location, snapshot)});
  // Keys without references are copied as a block (see copy_elements): all
  // of the keys array (of capacity elements), a constant number more often
  // than the length. Others are copied one by one, as many as there are.
  copy_elements(
    keys,
    field(map, "keys"),
    contains_reference(k, ns) ? field(map, "len") : field(map, "cap"),
    location,
    snapshot);
  snapshot.add(code_frontend_assignt{
    visited,
    allocate_elements(
      unsignedbv_typet{8}, field(map, "cap"), true, location, snapshot)});
  scope.add(code_ifthenelset{notequal_exprt{map, nil}, std::move(snapshot)});

  const std::string prefix =
    "$for" + std::to_string(state.label_counter++) + "$";
  const symbol_exprt step = new_temporary(int_type(), location, scope);
  code_blockt body;
  const symbol_exprt j = new_temporary(int_type(), location, scope);
  body.add(
    code_frontend_assignt{j, side_effect_expr_nondett{int_type(), location}});
  const dereference_exprt seen{plus_exprt{visited, j}};
  body.add(code_assumet{and_exprt{
    binary_relation_exprt{j, ID_ge, from_integer(0, int_type())},
    binary_relation_exprt{j, ID_lt, count},
    equal_exprt{seen, from_integer(0, seen.type())}}});
  body.add(code_frontend_assignt{seen, from_integer(1, seen.type())});
  const exprt key = dereference_exprt{plus_exprt{keys, j}};
  const symbol_exprt found = map_find(map, key, stmt, body);
  // Skip keys that are no longer present.
  body.add(code_ifthenelset{
    binary_relation_exprt{found, ID_lt, from_integer(0, int_type())},
    code_gotot{prefix + "next"}});

  const auto bind = [&](const irept &node, const exprt &value)
  {
    if(node.is_nil() || (node.id() == "Ident" && node.get("Name") == "_"))
      return;
    exprt target;
    if(stmt.get("Tok") == ":=")
    {
      typet type =
        convert_type(parse_tree.object(node.get("_obj")).get("type"), node);
      if(type.is_nil())
        return;
      target = declare_local(node.get("_obj"), type, location_of(node), body);
    }
    else
    {
      access_partst parts;
      target = convert_access(node, parts);
      if(target.is_nil())
        return;
      for(auto &code : parts.operands.statements())
        body.add(std::move(code));
      for(auto &code : parts.checks.statements())
        body.add(std::move(code));
    }
    code_frontend_assignt assign{target, assign_cast(value, target.type())};
    assign.add_source_location() = location_of(node);
    body.add(std::move(assign));
  };
  bind(go_field(stmt, "Key"), key);
  bind(
    go_field(stmt, "Value"),
    dereference_exprt{plus_exprt{field(map, "vals"), found}});

  state.jump_targets.push_back(
    {label_object, true, prefix + "break", prefix + "next"});
  code_blockt statements = convert_block(go_field(stmt, "Body"));
  state.jump_targets.pop_back();
  for(auto &code : statements.statements())
    body.add(std::move(code));
  body.add(code_labelt{prefix + "next", code_skipt{}});

  scope.add(counting_loop(step, count, std::move(body), location));
  if(!label_object.empty())
    scope.add(code_labelt{prefix + "break", code_skipt{}});
  dest.add(std::move(scope));
}
