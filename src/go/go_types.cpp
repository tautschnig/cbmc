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
#include <util/ieee_float.h>
#include <util/pointer_expr.h>
#include <util/std_types.h>
#include <util/symbol_table_base.h>

#include "go_parse_tree.h"

#include <map>

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
  {
    // Go1.27 §Boolean_types. Stored in a byte, as C's _Bool, so that
    // boolean variables have a size (e.g., on the heap); used as conditions
    // through as_condition (go_convert.cpp): any non-zero byte is true.
    return c_bool_typet{8};
  }
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
  if(kind == "string" || kind == "untyped string")
    return string_type();
  // Go1.27 §Numeric_types: "float32 the set of all IEEE 754 32-bit
  // floating-point numbers", "float64 the set of all IEEE 754 64-bit
  // floating-point numbers", "complex64 the set of all complex numbers with
  // float32 real and imaginary parts", complex128 with float64 parts.
  // Untyped constants default to float64 and complex128 (§Constants).
  if(kind == "float32")
    return ieee_float_spect::single_precision().to_type();
  if(kind == "float64" || kind == "untyped float")
    return ieee_float_spect::double_precision().to_type();
  if(kind == "complex64")
    return complex_type(ieee_float_spect::single_precision().to_type());
  if(kind == "complex128" || kind == "untyped complex")
    return complex_type(ieee_float_spect::double_precision().to_type());

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
    // Instances of generic types (with "targs") have their type arguments
    // substituted in their underlying type by go/types.
    if(underlying(index).get("k") == "struct")
      return named_struct(index, entry);
    if(underlying(index).get("k") == "signature")
      return named_signature(index, entry);
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

  // Stays nil if a part of the type is unsupported.
  typet result{ID_nil};
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
  else if(k == "interface")
  {
    // Go1.27 §Interface_types: "A variable of interface type can store a
    // value of any type that is in the type set of the interface." "The
    // value of an uninitialized variable of interface type
    // is nil."
    result = interface_type();
  }
  else if(k == "chan")
  {
    // Go1.27 §Channel_types: "The value of an uninitialized channel is
    // nil." Directions do not change the representation.
    typet element = (*this)(under.get("elem"));
    if(element.is_not_nil())
      result = chan_type(element);
  }
  else if(k == "map")
  {
    typet key = (*this)(under.get("key"));
    typet value = (*this)(under.get("elem"));
    if(key.is_not_nil() && value.is_not_nil())
      result = map_type(key, value);
  }
  else if(k == "slice")
  {
    typet element = (*this)(under.get("elem"));
    if(element.is_not_nil())
      result = slice_type(element);
  }
  else if(!under.get("reason").empty())
    unsupported_reason = under.get_string("reason");
  else
    unsupported_reason = "type " + entry.get_string("str");

  converting.erase(index);
  if(result.is_not_nil())
  {
    result.set(ID_C_go_type, entry.get("str"));
    result.set(ID_C_go_tid, type_id(index));
  }
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

/// The identifier of the type symbol of the named type \p entry.
static irep_idt named_type_id(const irept &entry)
{
  // Go1.27 §Instantiations: "Instantiating a generic type, function, or
  // method results in a non-generic type, function, or method,
  // respectively." Each instance of a generic type is a type of its own,
  // named after the type arguments ("iname").
  return go_symbol_id(
    entry.get("iname").empty() ? entry.get("obj") : entry.get("iname"));
}

void go_type_convertert::add_type_symbol(
  const irep_idt &id,
  const irept &entry,
  typet type)
{
  const irept &object = parse_tree.object(entry.get("obj"));
  symbolt symbol{id, std::move(type), "go"};
  symbol.base_name = object.get("name");
  symbol.pretty_name = entry.get("str");
  symbol.module = object.get("pkg");
  symbol.is_type = true;
  symbol_table.add(symbol);
  added_types.push_back(id);
}

void go_type_convertert::erase_type_symbols(std::size_t mark)
{
  // Types added since are reachable from the type whose conversion
  // failed; some of them may refer to its tag.
  for(std::size_t i = mark; i < added_types.size(); ++i)
  {
    const auto it = symbol_table.symbols.find(added_types[i]);
    if(it != symbol_table.symbols.end())
      symbol_table.erase(it);
  }
  added_types.resize(mark);
}

typet go_type_convertert::named_signature(
  const irep_idt &index,
  const irept &entry)
{
  // A named function type is its underlying type, unless it refers to
  // itself (e.g., `type stateFn func(*lexer) stateFn`): then it becomes a
  // struct tag naming its function values, the only way CBMC types can
  // have cycles. The type symbol is added once the conversion succeeds;
  // the occurrences inside the type already use the tag.
  const irep_idt id = named_type_id(entry);
  struct_tag_typet tag{id};
  tag.set(ID_C_go_type, entry.get("str"));
  tag.set(ID_C_go_tid, type_id(index));
  if(symbol_table.has_symbol(id))
    return std::move(tag);
  if(!converting.insert(index).second)
  {
    recursive.insert(index);
    return std::move(tag);
  }
  const std::size_t mark = added_types.size();
  typet code = signature(index);
  converting.erase(index);
  const bool is_recursive = recursive.erase(index) != 0;
  if(code.is_nil())
  {
    erase_type_symbols(mark);
    return typet{ID_nil};
  }
  struct_typet result = func_value(to_code_type(code));
  result.set(ID_C_go_type, entry.get("str"));
  result.set(ID_C_go_tid, type_id(index));
  if(!is_recursive)
    return std::move(result);
  result.set_tag(parse_tree.object(entry.get("obj")).get("name"));
  add_type_symbol(id, entry, std::move(result));
  return std::move(tag);
}

typet go_type_convertert::named_struct(
  const irep_idt &index,
  const irept &entry)
{
  // A struct tag, so that types such as `type Node struct{ next *Node }`
  // (Go1.27 §Struct_types: valid because the cycle passes through a
  // pointer) are finite. The type symbol is added before the fields are
  // converted.
  const irep_idt id = named_type_id(entry);
  struct_tag_typet tag{id};
  tag.set(ID_C_go_type, entry.get("str"));
  tag.set(ID_C_go_tid, type_id(index));
  if(symbol_table.has_symbol(id))
    return std::move(tag);

  const std::size_t mark = added_types.size();
  add_type_symbol(id, entry, struct_typet{});
  typet type = struct_type(underlying(index));
  if(type.is_nil())
  {
    erase_type_symbols(mark);
    return typet{ID_nil};
  }
  to_struct_type(type).set_tag(parse_tree.object(entry.get("obj")).get("name"));
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

typet go_type_convertert::result_type(const irep_idt &index)
{
  // Go1.27 §Function_types. A function with several results returns them
  // as one struct (see tuple), with components $0, $1, ...
  const auto &results = go_list(underlying(index), "results");
  if(results.empty())
    return empty_typet{};
  if(results.size() == 1)
    return (*this)(results.front().get("type"));
  return tuple(results);
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

  typet return_type = result_type(index);
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
  // Go1.27 §Passing_arguments_to_..._parameters: "If f is variadic with a
  // final parameter p of type ...T, then within f the type of p is
  // equivalent to type []T." go/types gives p that type; calls build the
  // slice (go_convertert::convert_arguments).
  if(under.get_bool("variadic"))
    result.set(ID_C_go_variadic, true);
  result.set(ID_C_go_type, parse_tree.type_entry(index).get("str"));
  return std::move(result);
}

struct_typet go_type_convertert::string_type()
{
  struct_typet::componentst components;
  components.emplace_back(
    "data", pointer_typet{unsignedbv_typet{8}, config.ansi_c.pointer_width});
  components.emplace_back("len", signedbv_typet{config.go.int_width});
  struct_typet result{std::move(components)};
  result.set(ID_C_go_string, true);
  result.set(ID_C_go_type, "string");
  return result;
}

struct_typet go_type_convertert::slice_type(const typet &element)
{
  struct_typet::componentst components;
  components.emplace_back(
    "data", pointer_typet{element, config.ansi_c.pointer_width});
  components.emplace_back("len", signedbv_typet{config.go.int_width});
  components.emplace_back("cap", signedbv_typet{config.go.int_width});
  struct_typet result{std::move(components)};
  result.set(ID_C_go_slice, true);
  return result;
}

pointer_typet go_type_convertert::chan_type(const typet &element)
{
  // A pointer to a header on the heap (see go_channels.cpp): the capacity,
  // the number of buffered values (for unbuffered channels: whether a
  // sender has deposited its value), the index of the first one, whether
  // the channel is closed, the numbers of values deposited and taken by
  // unbuffered communication, the number of receivers waiting, and the
  // buffer (one element for unbuffered channels).
  const signedbv_typet int_type{config.go.int_width};
  struct_typet::componentst components;
  components.emplace_back("cap", int_type);
  components.emplace_back("count", int_type);
  components.emplace_back("head", int_type);
  components.emplace_back("closed", c_bool_typet{8});
  components.emplace_back("sent", int_type);
  components.emplace_back("recvd", int_type);
  components.emplace_back("waiting", int_type);
  components.emplace_back(
    "buf", pointer_typet{element, config.ansi_c.pointer_width});
  struct_typet header{std::move(components)};
  header.set(ID_C_go_chan, true);
  return pointer_typet{std::move(header), config.ansi_c.pointer_width};
}

pointer_typet go_type_convertert::map_type(const typet &key, const typet &value)
{
  const signedbv_typet int_type{config.go.int_width};
  struct_typet::componentst components;
  components.emplace_back("len", int_type);
  components.emplace_back("cap", int_type);
  components.emplace_back(
    "keys", pointer_typet{key, config.ansi_c.pointer_width});
  components.emplace_back(
    "vals", pointer_typet{value, config.ansi_c.pointer_width});
  struct_typet header{std::move(components)};
  header.set(ID_C_go_map, true);
  return pointer_typet{std::move(header), config.ansi_c.pointer_width};
}

struct_typet go_type_convertert::complex_type(const typet &part)
{
  // A struct rather than CBMC's complex type, whose lowering
  // (remove_complex) does not cover function types.
  struct_typet result{{{"real", part}, {"imag", part}}};
  result.set(ID_C_go_complex, true);
  return result;
}

struct_typet go_type_convertert::interface_type()
{
  struct_typet::componentst components;
  components.emplace_back("type", signedbv_typet{32});
  components.emplace_back(
    "data", pointer_typet{empty_typet{}, config.ansi_c.pointer_width});
  struct_typet result{std::move(components)};
  result.set(ID_C_go_interface, true);
  return result;
}

std::size_t go_type_convertert::type_id(const irep_idt &index) const
{
  const irept &entry = parse_tree.type_entry(index);
  irep_idt canonical = entry.get("tid");
  const irept &under = underlying(index);
  if(entry.get("k") == "basic" && under.get("k") == "basic")
  {
    // Go1.27 §Constants: an untyped constant used as a value takes its
    // default type.
    const std::string kind = under.get_string("kind");
    static const std::map<std::string, std::string> defaults{
      {"untyped bool", "bool"},
      {"untyped int", "int"},
      {"untyped rune", "int32"},
      {"untyped float", "float64"},
      {"untyped complex", "complex128"},
      {"untyped string", "string"}};
    const auto it = defaults.find(kind);
    if(it != defaults.end())
    {
      for(std::size_t i = 0; i < parse_tree.types.size(); ++i)
      {
        const irept &candidate = parse_tree.types[i];
        if(candidate.get("k") == "basic" && candidate.get("kind") == it->second)
        {
          canonical = candidate.get("tid");
          break;
        }
      }
    }
  }
  return std::stoul(id2string(canonical)) + 1;
}

std::size_t go_type_convertert::basic_type_id(const std::string &kind) const
{
  for(std::size_t i = 0; i < parse_tree.types.size(); ++i)
  {
    const irept &candidate = parse_tree.types[i];
    if(candidate.get("k") == "basic" && candidate.get("kind") == kind)
      return std::stoul(id2string(candidate.get("tid"))) + 1;
  }
  return 0;
}

std::size_t go_type_convertert::pointer_type_id(std::size_t base) const
{
  for(std::size_t i = 0; i < parse_tree.types.size(); ++i)
  {
    const irept &candidate = parse_tree.types[i];
    if(
      candidate.get("k") == "pointer" && type_id(candidate.get("elem")) == base)
    {
      return std::stoul(id2string(candidate.get("tid"))) + 1;
    }
  }
  return 0;
}
