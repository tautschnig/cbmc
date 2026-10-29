/*******************************************************************\

Module: Go front-end: Go types to CBMC types

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// Mapping of go/types type-table entries to CBMC `typet`s.

#include "go_types.h"

#include <util/bitvector_types.h>
#include <util/config.h>
#include <util/pointer_expr.h>
#include <util/std_types.h>

#include "go_parse_tree.h"

const irept &go_type_convertert::underlying(const irep_idt &index) const
{
  const irept *entry = &parse_tree.type_entry(index);
  // Go1.27 §Underlying_types: a named type's underlying type is that of
  // its declaration; go/types records it as "under".
  for(int depth = 0; entry->get("k") == "named" && depth < 64; depth++)
    entry = &parse_tree.type_entry(entry->get("under"));
  return *entry;
}

irep_idt go_type_convertert::basic_kind(const irep_idt &index) const
{
  const irept &entry = underlying(index);
  return entry.get("k") == "basic" ? entry.get("kind") : irep_idt{};
}

bool go_type_convertert::is_untyped(const irep_idt &index) const
{
  return basic_kind(index).starts_with("untyped ");
}

bool go_type_convertert::is_signed_integer(const irep_idt &index) const
{
  const irep_idt kind = basic_kind(index);
  return kind == "int" || kind == "int8" || kind == "int16" ||
         kind == "int32" || kind == "int64" || kind == "untyped int" ||
         kind == "untyped rune";
}

typet go_type_convertert::basic(const irep_idt &kind)
{
  // Go1.27 §Numeric_types. int, uint and uintptr have implementation-
  // specific sizes; they follow gc on the target (config.go).
  if(kind == "bool" || kind == "untyped bool")
    return bool_typet{}; // Go1.27 §Boolean_types
  if(kind == "int8")
    return signedbv_typet{8};
  if(kind == "int16")
    return signedbv_typet{16};
  if(kind == "int32" || kind == "untyped rune") // rune is an alias for int32
    return signedbv_typet{32};
  if(kind == "int64")
    return signedbv_typet{64};
  if(kind == "int" || kind == "untyped int")
    return signedbv_typet{config.go.int_width};
  if(kind == "uint8") // byte is an alias for uint8
    return unsignedbv_typet{8};
  if(kind == "uint16")
    return unsignedbv_typet{16};
  if(kind == "uint32")
    return unsignedbv_typet{32};
  if(kind == "uint64")
    return unsignedbv_typet{64};
  if(kind == "uint")
    return unsignedbv_typet{config.go.int_width};
  if(kind == "uintptr")
    return unsignedbv_typet{config.go.uintptr_width};

  unsupported_reason = "type " + id2string(kind);
  return typet{ID_nil};
}

typet go_type_convertert::operator()(const irep_idt &index)
{
  const irept &entry = parse_tree.type_entry(index);
  if(entry.is_nil())
  {
    unsupported_reason = "missing type information";
    return typet{ID_nil};
  }
  const irept &under = underlying(index);
  const irep_idt &k = under.get("k");

  typet result;
  if(k == "basic")
  {
    // Go1.27 §Constants: an untyped constant that is not given a type by
    // its context takes its default type (bool, rune, int, ...); the
    // "untyped" kinds above map to those defaults.
    result = basic(under.get("kind"));
  }
  else if(k == "signature")
  {
    // Go1.27 §Function_types: a function value refers to a function, or
    // is nil; it is modelled as a pointer to code.
    typet code = signature(index);
    if(code.is_nil())
      return typet{ID_nil};
    result = pointer_typet{std::move(code), config.ansi_c.pointer_width};
  }
  else if(k == "tuple")
  {
    // The type of a call of a function with several results.
    result = tuple(go_list(under, "items"));
    if(result.is_nil())
      return typet{ID_nil};
  }
  else
  {
    unsupported_reason = "type " + entry.get_string("str");
    return typet{ID_nil};
  }

  if(result.is_not_nil())
    result.set(ID_C_go_type, entry.get("str"));
  return result;
}

typet go_type_convertert::tuple(const irept::subt &items)
{
  struct_typet::componentst components;
  for(const auto &item : items)
  {
    typet type = (*this)(item.get("type"));
    if(type.is_nil())
      return typet{ID_nil};
    components.emplace_back(
      "$" + std::to_string(components.size()), std::move(type));
  }
  struct_typet result{std::move(components)};
  result.set(ID_C_go_tuple, true);
  return std::move(result);
}

typet go_type_convertert::signature(const irep_idt &index)
{
  const irept &under = underlying(index);
  if(under.get("k") != "signature")
  {
    unsupported_reason =
      "type " + parse_tree.type_entry(index).get_string("str");
    return typet{ID_nil};
  }
  if(under.get_bool("variadic"))
  {
    unsupported_reason = "variadic functions";
    return typet{ID_nil};
  }

  // Go1.27 §Function_types. A function with several results returns them
  // as one struct (see tuple), with components $0, $1, ...
  const auto &results = go_list(under, "results");
  typet return_type = empty_typet{};
  if(results.size() == 1)
    return_type = (*this)(results.front().get("type"));
  else if(results.size() > 1)
    return_type = tuple(results);
  if(return_type.is_nil())
    return typet{ID_nil};

  code_typet::parameterst parameters;
  for(const auto &param : go_list(under, "params"))
  {
    typet param_type = (*this)(param.get("type"));
    if(param_type.is_nil())
      return typet{ID_nil};
    parameters.emplace_back(param_type);
  }
  code_typet result{std::move(parameters), std::move(return_type)};
  result.set(ID_C_go_type, parse_tree.type_entry(index).get("str"));
  return std::move(result);
}
