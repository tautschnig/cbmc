/*******************************************************************\

Module: Go front-end: Go types to CBMC types

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// Mapping of go/types type-table entries to CBMC `typet`s.

#include "go_types.h"

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/config.h>
#include <util/pointer_expr.h>
#include <util/std_types.h>
#include <util/symbol_table_base.h>

#include "go_parse_tree.h"

irep_idt go_symbol_id(const irep_idt &object_id)
{
  const std::string id = id2string(object_id);
  const auto hash = id.rfind('#');
  if(hash == std::string::npos)
    return "go::" + id;
  const std::string qualified = id.substr(0, hash); // pkg.name
  const auto dot = qualified.rfind('.');            // pkg may contain dots
  return "go::" + qualified.substr(0, dot) + "::" + id.substr(hash + 1) +
         "::" + qualified.substr(dot + 1);
}

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
  if(entry.get("k") == "named")
  {
    if(!go_list(entry, "targs").empty())
    {
      unsupported_reason =
        "instances of generic type " + entry.get_string("str");
      return typet{ID_nil};
    }
    if(underlying(index).get("k") == "struct")
      return named_struct(index, entry);
    // Other named types are their underlying type (Go1.27
    // §Underlying_types); only struct tags can break a cycle.
    if(!converting.insert(index).second)
    {
      unsupported_reason =
        "type " + entry.get_string("str") + " that refers to itself";
      return typet{ID_nil};
    }
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
    // Go1.27 §Function_types: "The value of an uninitialized variable of
    // function type is nil."
    typet code = signature(index);
    if(code.is_not_nil())
      result = func_value(to_code_type(code));
  }
  else if(k == "tuple")
  {
    // The type of a call of a function with several results.
    result = tuple(go_list(under, "items"));
  }
  else if(k == "pointer")
  {
    // Go1.27 §Pointer_types: "The value of an uninitialized pointer is
    // nil."
    typet base = (*this)(under.get("elem"));
    if(base.is_not_nil())
      result = pointer_typet{std::move(base), config.ansi_c.pointer_width};
  }
  else if(k == "array")
  {
    // Go1.27 §Array_types: "The length is part of the array's type".
    typet element = (*this)(under.get("elem"));
    if(element.is_not_nil())
    {
      result = array_typet{
        std::move(element),
        from_integer(string2integer(under.get_string("len")), c_index_type())};
    }
  }
  else if(k == "struct")
    result = struct_type(under);
  else
    unsupported_reason = "type " + entry.get_string("str");

  converting.erase(index);
  if(result.is_not_nil())
    result.set(ID_C_go_type, entry.get("str"));
  return result;
}

typet go_type_convertert::struct_type(const irept &entry)
{
  // Go1.27 §Struct_types: "A struct is a sequence of named elements,
  // called fields". An embedded field is named after its type. Blank
  // fields keep their position, under unique names.
  struct_typet::componentst components;
  for(const auto &field : go_list(entry, "fields"))
  {
    typet type = (*this)(field.get("type"));
    if(type.is_nil())
      return typet{ID_nil};
    std::string name = field.get_string("name");
    if(name == "_")
      name = "_$" + std::to_string(components.size());
    components.emplace_back(name, std::move(type));
    components.back().set_base_name(field.get("name"));
    components.back().set_pretty_name(field.get("name"));
  }
  return struct_typet{std::move(components)};
}

typet go_type_convertert::named_struct(
  const irep_idt &index,
  const irept &entry)
{
  // A struct tag, so that types such as `type Node struct{ next *Node }`
  // (Go1.27 §Struct_types: valid because the cycle passes through a
  // pointer) are finite. The type symbol is added before the fields are
  // converted.
  const irep_idt &object_id = entry.get("obj");
  const irep_idt id = go_symbol_id(object_id);
  struct_tag_typet tag{id};
  tag.set(ID_C_go_type, entry.get("str"));
  if(symbol_table.has_symbol(id))
    return std::move(tag);

  const irept &object = parse_tree.object(object_id);
  symbolt symbol{id, struct_typet{}, "go"};
  symbol.base_name = object.get("name");
  symbol.pretty_name = entry.get("str");
  symbol.module = object.get("pkg");
  symbol.is_type = true;
  symbol_table.add(symbol);

  typet type = struct_type(underlying(index));
  if(type.is_nil())
  {
    symbol_table.erase(symbol_table.symbols.find(id));
    return typet{ID_nil};
  }
  to_struct_type(type).set_tag(symbol.base_name);
  type.set(ID_C_go_type, entry.get("str"));
  symbol_table.get_writeable_ref(id).type = std::move(type);
  return std::move(tag);
}

pointer_typet go_type_convertert::env_pointer_type()
{
  return pointer_typet{empty_typet{}, config.ansi_c.pointer_width};
}

struct_typet go_type_convertert::func_value(const code_typet &code)
{
  code_typet with_env = code;
  with_env.parameters().insert(
    with_env.parameters().begin(),
    code_typet::parametert{typet{env_pointer_type()}});
  struct_typet::componentst components;
  components.emplace_back(
    "code", pointer_typet{std::move(with_env), config.ansi_c.pointer_width});
  components.emplace_back("env", env_pointer_type());
  struct_typet result{std::move(components)};
  result.set(ID_C_go_func_value, true);
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
