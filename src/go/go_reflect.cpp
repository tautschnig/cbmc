/*******************************************************************\

Module: Go front-end: support of the model of package reflect

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// Type descriptors and intrinsics for models/reflect (the model of package
/// reflect, which replaces it, see the helper's models.go).
///
/// Each type that a reflected value may have gets a descriptor, a global
/// variable `go::reflect.$type.<tid>` of the model's type `rtype`, built
/// from the helper's type table: its kind, its strings, gc's size and
/// alignment, its element, key, field, parameter and result types (as
/// pointers to their descriptors), its methods, and the front-end's own
/// layout of its values (the size of a value, and the offsets of fields)
/// for the model's address arithmetic. The described types are the dynamic
/// types of interface values and the types they refer to.
///
/// The model's intrinsics (bodiless functions of models/reflect, see
/// intrinsics.go there) operate on a value of a type given by its id: their
/// bodies test the id against every described type, as the functions of
/// go_interfaces.cpp test dynamic types.

#include <util/arith_tools.h>
#include <util/c_types.h>
#include <util/config.h>
#include <util/expr_initializer.h>
#include <util/pointer_expr.h>
#include <util/pointer_offset_size.h>
#include <util/std_code.h>
#include <util/symbol_table_base.h>

#include "go_convert.h"
#include "go_parse_tree.h"

#include <algorithm>

static const irep_idt rtype_id = "go::reflect.rtype";

static irep_idt descriptor_id(std::size_t tid)
{
  return "go::reflect.$type." + std::to_string(tid);
}

/// reflect.Kind of the type-table entry \p under (an underlying type).
static unsigned reflect_kind(const irept &under)
{
  const irep_idt &k = under.get("k");
  if(k == "basic")
  {
    static const std::vector<std::string> kinds{
      "",
      "bool",
      "int",
      "int8",
      "int16",
      "int32",
      "int64",
      "uint",
      "uint8",
      "uint16",
      "uint32",
      "uint64",
      "uintptr",
      "float32",
      "float64",
      "complex64",
      "complex128"};
    const std::string &kind = under.get_string("kind");
    const auto it = std::find(kinds.begin(), kinds.end(), kind);
    if(it != kinds.end())
      return static_cast<unsigned>(it - kinds.begin());
    if(kind == "string")
      return 24;
    if(kind == "unsafe.Pointer")
      return 26;
    return 0;
  }
  if(k == "array")
    return 17;
  if(k == "chan")
    return 18;
  if(k == "signature")
    return 19;
  if(k == "interface")
    return 20;
  if(k == "map")
    return 21;
  if(k == "pointer")
    return 22;
  if(k == "slice")
    return 23;
  if(k == "struct")
    return 25;
  return 0;
}

/// The methods \p list (of a method set or an interface) sorted by name,
/// as the descriptors list them.
static std::vector<const irept *> sorted_methods(const irept::subt &list)
{
  std::vector<const irept *> sorted;
  for(const auto &m : list)
    sorted.push_back(&m);
  std::stable_sort(
    sorted.begin(),
    sorted.end(),
    [](const irept *a, const irept *b)
    { return a->get_string("name") < b->get_string("name"); });
  return sorted;
}

/// The value of struct type \p type with components \p values (by name),
/// the others zero.
static exprt struct_value(
  const typet &type,
  const std::map<irep_idt, exprt> &values,
  const namespacet &ns,
  const source_locationt &location)
{
  const struct_typet &followed = type.id() == ID_struct_tag
                                   ? ns.follow_tag(to_struct_tag_type(type))
                                   : to_struct_type(type);
  exprt::operandst operands;
  for(const auto &c : followed.components())
  {
    const auto it = values.find(c.get_name());
    if(it == values.end())
      operands.push_back(*zero_initializer(c.type(), location, ns));
    else
      operands.push_back(
        typecast_exprt::conditional_cast(it->second, c.type()));
  }
  return struct_exprt{std::move(operands), type};
}

/// The type of component \p name of struct (tag) type \p type.
static const typet &
component_type(const typet &type, const irep_idt &name, const namespacet &ns)
{
  const struct_typet &followed = type.id() == ID_struct_tag
                                   ? ns.follow_tag(to_struct_tag_type(type))
                                   : to_struct_type(type);
  return followed.get_component(name).type();
}

/// A slice of type \p slice_type (of the model) with elements \p elements,
/// stored in global array \p array_id.
static exprt static_slice(
  symbol_table_baset &symbol_table,
  const irep_idt &array_id,
  const typet &slice_type,
  exprt::operandst elements,
  const namespacet &ns)
{
  const typet &data_type = component_type(slice_type, "data", ns);
  const typet &length_type = component_type(slice_type, "len", ns);
  const std::size_t n = elements.size();
  if(n == 0)
  {
    return struct_exprt{
      {null_pointer_exprt{to_pointer_type(data_type)},
       from_integer(0, length_type),
       from_integer(0, length_type)},
      slice_type};
  }
  const typet &element = to_pointer_type(data_type).base_type();
  const array_typet array_type{element, from_integer(n, c_index_type())};
  symbolt symbol{array_id, array_type, "go"};
  symbol.base_name = array_id;
  symbol.is_static_lifetime = true;
  symbol.is_lvalue = true;
  symbol.value = array_exprt{std::move(elements), array_type};
  symbol_table.add(symbol);
  return struct_exprt{
    {address_of_exprt{
       index_exprt{symbol.symbol_expr(), from_integer(0, c_index_type())},
       to_pointer_type(data_type)},
     from_integer(n, length_type),
     from_integer(n, length_type)},
    slice_type};
}

exprt go_convertert::reflect_descriptor(
  std::size_t tid,
  const std::map<std::size_t, typet> &described,
  const source_locationt &location)
{
  const irep_idt index = type_converter.type_index(tid);
  const irept &entry = parse_tree.type_entry(index);
  const irept &under = type_converter.underlying(index);
  const typet &type = described.at(tid);
  const struct_tag_typet rtype{rtype_id};
  const pointer_typet rtype_pointer{rtype, config.ansi_c.pointer_width};
  // A pointer to the descriptor of the type at type-table index i, or nil
  // (for types the model does not describe).
  const auto descriptor = [&](const irep_idt &i) -> exprt
  {
    if(i.empty())
      return null_pointer_exprt{rtype_pointer};
    const std::size_t t = type_converter.type_id(i);
    if(described.count(t) == 0)
      return null_pointer_exprt{rtype_pointer};
    return address_of_exprt{symbol_exprt{descriptor_id(t), rtype}};
  };
  const auto number = [](const irept &node, const irep_idt &field)
  { return string2integer(node.get_string(field)); };
  // The package path of the object (a field or method) with id \p obj if
  // its name is not exported, as reflect reports (empty otherwise).
  const auto unexported_pkg = [&](const std::string &name, const irep_idt &obj)
  {
    if(name.empty() || (name[0] >= 'A' && name[0] <= 'Z'))
      return std::string{};
    std::string pkg = parse_tree.object(obj).get_string("pkg");
    const std::string prefix = "cprover/std/";
    if(pkg.compare(0, prefix.size(), prefix) == 0)
      pkg = pkg.substr(prefix.size());
    return pkg;
  };

  std::map<irep_idt, exprt> values;
  values["tid"] = from_integer(tid, signedbv_typet{32});
  values["kind"] =
    from_integer(reflect_kind(under), component_type(rtype, "kind", ns));
  values["str"] = string_constant(entry.get_string("rstr"), location);
  values["name"] = string_constant(entry.get_string("rname"), location);
  values["pkgPath"] = string_constant(entry.get_string("rpkg"), location);
  const auto number_value = [&](const irep_idt &field, const mp_integer &n)
  { values[field] = from_integer(n, component_type(rtype, field, ns)); };
  number_value("size", number(entry, "size"));
  number_value("align", number(entry, "align"));
  number_value("fieldAlign", number(entry, "falign"));
  const auto stride = pointer_offset_size(type, ns);
  number_value("stride", stride.value_or(0));
  values["comparable"] = from_integer(
    is_comparable(type) ? 1 : 0, component_type(rtype, "comparable", ns));
  values["elem"] = descriptor(under.get("elem"));
  values["key"] = descriptor(under.get("key"));
  values["ptrTo"] = descriptor(entry.get("ptr"));
  values["under"] = entry.get("k") == "named" ? descriptor(entry.get("under"))
                                              : descriptor(index);
  if(under.get("k") == "array")
    number_value("len", number(under, "len"));
  if(under.get("k") == "chan")
  {
    const irep_idt &dir = under.get("dir");
    number_value("dir", dir == "recv" ? 1 : dir == "send" ? 2 : 3);
  }
  if(under.get("k") == "signature")
  {
    number_value("variadic", under.get_bool("variadic") ? 1 : 0);
    for(const char *which : {"params", "results"})
    {
      exprt::operandst items;
      for(const auto &item : go_list(under, which))
        items.push_back(descriptor(item.get("type")));
      const irep_idt field = std::string{which} == "params" ? "in" : "out";
      values[field] = static_slice(
        symbol_table,
        id2string(descriptor_id(tid)) + "." + id2string(field),
        component_type(rtype, field, ns),
        std::move(items),
        ns);
    }
  }

  // Fields, with the front-end's offsets (of the components of the CBMC
  // struct, in the same order).
  if(under.get("k") == "struct")
  {
    const typet &fields_type = component_type(rtype, "fields", ns);
    const typet &field_type =
      to_pointer_type(component_type(fields_type, "data", ns)).base_type();
    const struct_typet &followed = type.id() == ID_struct_tag
                                     ? ns.follow_tag(to_struct_tag_type(type))
                                     : to_struct_type(type);
    const auto &offsets = go_list(entry, "offsets");
    exprt::operandst fields;
    std::size_t i = 0;
    for(const auto &f : go_list(under, "fields"))
    {
      const std::string name = f.get_string("name");
      const auto &component = followed.components().at(i);
      const auto off = member_offset(followed, component.get_name(), ns);
      std::map<irep_idt, exprt> field;
      field["name"] = string_constant(name, location);
      field["pkgPath"] =
        string_constant(unexported_pkg(name, f.get("obj")), location);
      field["typ"] = descriptor(f.get("type"));
      field["tag"] = string_constant(f.get_string("tag"), location);
      field["offset"] = from_integer(
        i < offsets.size() ? string2integer(offsets[i].id_string()) : 0,
        component_type(field_type, "offset", ns));
      field["off"] =
        from_integer(off.value_or(0), component_type(field_type, "off", ns));
      field["embedded"] = from_integer(
        f.get_bool("embedded") ? 1 : 0,
        component_type(field_type, "embedded", ns));
      fields.push_back(struct_value(field_type, field, ns, location));
      ++i;
    }
    values["fields"] = static_slice(
      symbol_table,
      id2string(descriptor_id(tid)) + ".fields",
      fields_type,
      std::move(fields),
      ns);
  }

  // Methods: the method set of non-interface types (Go1.27 §Method_sets),
  // the methods of interfaces; both sorted by name, as go/types sorts
  // them (exported names first are not needed: the model skips unexported
  // ones where reflect does).
  {
    const typet &methods_type = component_type(rtype, "methods", ns);
    const typet &method_type =
      to_pointer_type(component_type(methods_type, "data", ns)).base_type();
    const bool is_interface = under.get("k") == "interface";
    const auto &list =
      is_interface ? go_list(under, "methods") : go_list(entry, "mset");
    const std::vector<const irept *> sorted = sorted_methods(list);
    exprt::operandst methods;
    for(const irept *m : sorted)
    {
      const std::string name = m->get_string("name");
      std::map<irep_idt, exprt> method;
      method["name"] = string_constant(name, location);
      method["pkgPath"] = string_constant(
        is_interface ? std::string{} : unexported_pkg(name, m->get("obj")),
        location);
      method["typ"] = descriptor(m->get("type"));
      method["sig"] = from_integer(
        type_converter.type_id(m->get("type")),
        component_type(method_type, "sig", ns));
      methods.push_back(struct_value(method_type, method, ns, location));
    }
    values["methods"] = static_slice(
      symbol_table,
      id2string(descriptor_id(tid)) + ".methods",
      methods_type,
      std::move(methods),
      ns);
  }
  return struct_value(rtype, values, ns, location);
}

void go_convertert::finish_reflect()
{
  if(!symbol_table.has_symbol(rtype_id))
    return;
  const source_locationt location;

  // The described types: the dynamic types, and the types they refer to
  // (elements, keys, fields, parameters, results, methods), and the
  // pointer types to those that are not pointers (for reflect.New).
  std::map<std::size_t, typet> described;
  std::vector<std::size_t> work;
  for(const auto &t : dynamic_types)
    work.push_back(t.first);
  const auto add = [&](const irep_idt &index)
  {
    if(!index.empty())
      work.push_back(type_converter.type_id(index));
  };
  // With reflect.MapOf, ArrayOf, ChanOf or SliceOf, also the types of the
  // program that these may return: those of the kind whose element (and
  // key) types are described (see constructed below).
  const auto uses = [&](const char *name)
  { return parse_tree.reflect_uses.count(name) != 0; };
  std::set<irep_idt> constructed_kinds;
  if(uses("MapOf"))
    constructed_kinds.insert("map");
  if(uses("ArrayOf"))
    constructed_kinds.insert("array");
  if(uses("ChanOf"))
    constructed_kinds.insert("chan");
  if(uses("SliceOf") || uses("Value.Slice") || uses("Value.Slice3"))
    constructed_kinds.insert("slice");
  const auto is_described = [&](const irep_idt &index)
  {
    return index.empty() || described.count(type_converter.type_id(index)) != 0;
  };
  for(bool more = true; more;)
  {
    more = false;
    while(!work.empty())
    {
      const std::size_t tid = work.back();
      work.pop_back();
      if(described.count(tid) != 0)
        continue;
      const irep_idt index = type_converter.type_index(tid);
      const irept &entry = parse_tree.type_entry(index);
      if(entry.get("rstr").empty() && entry.get("size").empty())
        continue; // not a type of values (see the helper's reflectInfo)
      const typet type = type_converter(index);
      if(type.is_nil())
        continue;
      described.emplace(tid, type);
      const irept &under = type_converter.underlying(index);
      add(under.get("elem"));
      add(under.get("key"));
      for(const auto &f : go_list(under, "fields"))
        add(f.get("type"));
      for(const char *which : {"params", "results"})
        for(const auto &item : go_list(under, which))
          add(item.get("type"));
      for(const auto &m : go_list(under, "methods"))
        add(m.get("type"));
      for(const auto &m : go_list(entry, "mset"))
        add(m.get("type"));
      if(under.get("k") != "pointer")
        add(entry.get("ptr"));
      if(entry.get("k") == "named")
        add(entry.get("under"));
      add(entry.get("slice"));
    }
    if(constructed_kinds.empty())
      break;
    for(std::size_t i = 0; i < parse_tree.types.size(); ++i)
    {
      const irep_idt index = std::to_string(i);
      const std::size_t tid = type_converter.type_id(index);
      if(described.count(tid) != 0 || type_converter.type_index(tid) != index)
        continue;
      const irept &entry = parse_tree.type_entry(index);
      const irep_idt &k = entry.get("k");
      if(
        constructed_kinds.count(k) != 0 && is_described(entry.get("elem")) &&
        is_described(entry.get("key")))
      {
        work.push_back(tid);
        more = true;
      }
    }
  }

  // The descriptors (declared first: they refer to each other).
  const struct_tag_typet rtype{rtype_id};
  for(const auto &t : described)
  {
    symbolt symbol{descriptor_id(t.first), rtype, "go"};
    symbol.base_name = symbol.name;
    symbol.is_static_lifetime = true;
    symbol.is_lvalue = true;
    symbol_table.add(symbol);
  }
  for(const auto &t : described)
  {
    exprt value = reflect_descriptor(t.first, described, location);
    symbol_table.get_writeable_ref(descriptor_id(t.first)).value =
      std::move(value);
  }
  // A reflected value may be converted to an interface value (by
  // Value.Interface), so all of these are dynamic types.
  for(const auto &t : described)
  {
    if(!is_go_interface(t.second))
      dynamic_types.emplace(t.first, t.second);
  }

  // The intrinsics. Each tests its type id (its first parameter) against
  // each described type that \p applies to, with the code of \p branch.
  const auto generate =
    [&](
      const irep_idt &id,
      const std::function<bool(const typet &)> &applies,
      const std::function<void(
        const typet &, const std::vector<exprt> &, code_blockt &)> &branch)
  {
    symbolt *function = symbol_table.get_writeable(id);
    if(function == nullptr || function->value.is_not_nil())
      return;
    const std::vector<exprt> params = parameter_symbols(*function);
    const typet result = to_code_type(function->type).return_type();
    function_statet outer = std::move(state);
    state = function_statet{};
    state.function_id = function->name;
    code_blockt body;
    for(const auto &t : described)
    {
      if(!applies(t.second))
        continue;
      // A branch with an unsupported construct (e.g., a comparison that
      // the front-end does not support) fails when reached.
      code_blockt block;
      std::vector<std::string> messages;
      unsupported_in_body = &messages;
      branch(t.second, params, block);
      unsupported_in_body = nullptr;
      if(!messages.empty())
        block = unsupported_body(function->location, messages.front());
      body.add(code_ifthenelset{
        equal_exprt{params[0], from_integer(t.first, params[0].type())},
        std::move(block)});
    }
    body.append(unsupported_body(
      function->location,
      "reflect: an operation on a value of a type the model does not "
      "describe"));
    end_generated_function(body, result);
    state = std::move(outer);
    function->value = std::move(body);
  };
  // *(T *)p: p may point into an object (a field or element), so this is
  // not a typed dereference (see unbox).
  const auto at = [](const exprt &p, const typet &type)
  {
    return dereference_exprt{
      typecast_exprt{p, pointer_typet{type, config.ansi_c.pointer_width}}};
  };
  const auto any = [](const typet &) { return true; };
  const auto not_interface = [](const typet &type)
  { return !is_go_interface(type); };
  const auto return_value =
    [](const exprt &value, const typet &result, code_blockt &block)
  {
    block.add(
      code_frontend_returnt{typecast_exprt::conditional_cast(value, result)});
  };

  // typeOf(t int32) *rtype: the descriptor of the type with id t, nil
  // for 0 (nil interface values).
  if(symbolt *function = symbol_table.get_writeable("go::reflect.typeOf"))
  {
    if(function->value.is_nil())
    {
      const exprt t = parameter_symbols(*function)[0];
      const typet result = to_code_type(function->type).return_type();
      code_blockt body;
      for(const auto &d : described)
      {
        body.add(code_ifthenelset{
          equal_exprt{t, from_integer(d.first, t.type())},
          code_frontend_returnt{typecast_exprt::conditional_cast(
            address_of_exprt{symbol_exprt{descriptor_id(d.first), rtype}},
            result)}});
      }
      body.add(
        code_frontend_returnt{null_pointer_exprt{to_pointer_type(result)}});
      function->value = std::move(body);
    }
  }
  // sliceOf(e) *rtype, mapOf(k, e), arrayOf(n, e), chanOf(dir, e): the
  // descriptor of the described type of that kind with these components,
  // or nil.
  const auto descriptor_of = [&](std::size_t tid) -> exprt {
    return address_of_exprt{symbol_exprt{descriptor_id(tid), rtype}};
  };
  const auto component_descriptor = [&](const irept &entry, const char *field)
  { return descriptor_of(type_converter.type_id(entry.get(field))); };
  for(const char *kind : {"slice", "map", "array", "chan"})
  {
    const irep_idt id = "go::reflect." + std::string{kind} + "Of";
    symbolt *function = symbol_table.get_writeable(id);
    if(function == nullptr || function->value.is_not_nil())
      continue;
    const std::vector<exprt> params = parameter_symbols(*function);
    const typet result = to_code_type(function->type).return_type();
    code_blockt body;
    for(const auto &d : described)
    {
      const irep_idt index = type_converter.type_index(d.first);
      const irept &entry = parse_tree.type_entry(index);
      if(entry.get("k") != kind)
        continue;
      exprt::operandst conditions;
      const exprt &elem = params.back();
      conditions.push_back(equal_exprt{
        elem,
        typecast_exprt::conditional_cast(
          component_descriptor(entry, "elem"), elem.type())});
      if(std::string{kind} == "map")
      {
        conditions.push_back(equal_exprt{
          params[0],
          typecast_exprt::conditional_cast(
            component_descriptor(entry, "key"), params[0].type())});
      }
      else if(std::string{kind} == "array")
      {
        conditions.push_back(equal_exprt{
          params[0],
          from_integer(
            string2integer(entry.get_string("len")), params[0].type())});
      }
      else if(std::string{kind} == "chan")
      {
        const irep_idt &dir = entry.get("dir");
        conditions.push_back(equal_exprt{
          params[0],
          from_integer(
            dir == "recv"   ? 1
            : dir == "send" ? 2
                            : 3,
            params[0].type())});
      }
      body.add(code_ifthenelset{
        conjunction(std::move(conditions)),
        code_frontend_returnt{
          typecast_exprt::conditional_cast(descriptor_of(d.first), result)}});
    }
    body.add(
      code_frontend_returnt{null_pointer_exprt{to_pointer_type(result)}});
    function->value = std::move(body);
  }
  // ifaceType(i any) int32, ifaceData(i any) unsafe.Pointer.
  for(const char *name : {"go::reflect.ifaceType", "go::reflect.ifaceData"})
  {
    if(symbolt *function = symbol_table.get_writeable(name))
    {
      if(function->value.is_nil())
      {
        const exprt i = parameter_symbols(*function)[0];
        const typet result = to_code_type(function->type).return_type();
        const struct_typet &iface = to_struct_type(i.type());
        const exprt field = member_exprt{
          i,
          iface.components()
            [std::string{name} == "go::reflect.ifaceType" ? 0 : 1]};
        function->value = code_blockt{{code_frontend_returnt{
          typecast_exprt::conditional_cast(field, result)}}};
      }
    }
  }
  // makeIface(t int32, p unsafe.Pointer) any: the interface value of
  // dynamic type t whose value is at p (which must not change).
  if(symbolt *function = symbol_table.get_writeable("go::reflect.makeIface"))
  {
    if(function->value.is_nil())
    {
      const std::vector<exprt> params = parameter_symbols(*function);
      const typet result = to_code_type(function->type).return_type();
      const struct_typet &iface = to_struct_type(result);
      function->value = code_blockt{{code_frontend_returnt{struct_exprt{
        {typecast_exprt::conditional_cast(
           params[0], iface.components()[0].type()),
         typecast_exprt::conditional_cast(
           params[1], iface.components()[1].type())},
        result}}}};
    }
  }
  // box(t int32, p unsafe.Pointer) any: an interface value holding a copy
  // of the value of type t at p.
  generate(
    "go::reflect.box",
    not_interface,
    [&](const typet &type, const std::vector<exprt> &p, code_blockt &block)
    { block.add(code_frontend_returnt{box(at(p[1], type), location)}); });
  // assign(t int32, dst, src unsafe.Pointer): *dst = *src.
  generate(
    "go::reflect.assign",
    any,
    [&](const typet &type, const std::vector<exprt> &p, code_blockt &block)
    {
      block.add(code_frontend_assignt{at(p[1], type), at(p[2], type)});
      block.add(code_frontend_returnt{});
    });
  // alloc(t int32) unsafe.Pointer: a new variable of type t, zero.
  generate(
    "go::reflect.alloc",
    any,
    [&](const typet &type, const std::vector<exprt> &, code_blockt &block)
    {
      const symbol_exprt pointer =
        new_heap_variable(type, nil_exprt{}, location, block);
      return_value(
        pointer,
        to_code_type(ns.lookup("go::reflect.alloc").type).return_type(),
        block);
    });
  // equal(t int32, x, y unsafe.Pointer) bool: *x == *y (Go1.27
  // §Comparison_operators), for comparable types.
  generate(
    "go::reflect.equal",
    [&](const typet &type) { return is_comparable(type); },
    [&](const typet &type, const std::vector<exprt> &p, code_blockt &block)
    {
      return_value(
        as_condition(equality(at(p[1], type), at(p[2], type), irept{})),
        to_code_type(ns.lookup("go::reflect.equal").type).return_type(),
        block);
    });
  // Maps: mapFind(t, m, k) int, the index of the entry of map *m with key
  // *k or -1; mapStore(t, m, k, v), (*m)[*k] = *v; mapDelete(t, m, k);
  // makeMap(t, m), *m = make(T).
  const auto is_map = [](const typet &type) { return is_go_map(type); };
  generate(
    "go::reflect.mapFind",
    is_map,
    [&](const typet &type, const std::vector<exprt> &p, code_blockt &block)
    {
      const typet &header = to_pointer_type(type).base_type();
      const typet &key =
        to_pointer_type(to_struct_type(header).get_component("keys").type())
          .base_type();
      const symbol_exprt found =
        map_find(at(p[1], type), at(p[2], key), irept{}, block);
      return_value(
        found,
        to_code_type(ns.lookup("go::reflect.mapFind").type).return_type(),
        block);
    });
  generate(
    "go::reflect.mapStore",
    is_map,
    [&](const typet &type, const std::vector<exprt> &p, code_blockt &block)
    {
      const struct_typet &header =
        to_struct_type(to_pointer_type(type).base_type());
      const typet &key =
        to_pointer_type(header.get_component("keys").type()).base_type();
      const typet &value =
        to_pointer_type(header.get_component("vals").type()).base_type();
      const symbol_exprt map = new_temporary(type, location, block);
      block.add(code_frontend_assignt{map, at(p[1], type)});
      map_insert(map, at(p[2], key), at(p[3], value), irept{}, block);
      block.add(code_frontend_returnt{});
    });
  generate(
    "go::reflect.mapDelete",
    is_map,
    [&](const typet &type, const std::vector<exprt> &p, code_blockt &block)
    {
      const struct_typet &header =
        to_struct_type(to_pointer_type(type).base_type());
      const typet &key =
        to_pointer_type(header.get_component("keys").type()).base_type();
      const symbol_exprt map = new_temporary(type, location, block);
      block.add(code_frontend_assignt{map, at(p[1], type)});
      const symbol_exprt found = map_find(map, at(p[2], key), irept{}, block);
      const auto field = [&](const char *name) {
        return member_exprt{dereference_exprt{map}, header.get_component(name)};
      };
      code_blockt remove;
      const exprt last =
        minus_exprt{field("len"), from_integer(1, field("len").type())};
      for(const char *array : {"keys", "vals"})
      {
        remove.add(code_frontend_assignt{
          dereference_exprt{plus_exprt{field(array), found}},
          dereference_exprt{plus_exprt{field(array), last}}});
      }
      remove.add(code_frontend_assignt{field("len"), last});
      block.add(code_ifthenelset{
        binary_relation_exprt{found, ID_ge, from_integer(0, found.type())},
        std::move(remove)});
      block.add(code_frontend_returnt{});
    });
  generate(
    "go::reflect.makeMap",
    is_map,
    [&](const typet &type, const std::vector<exprt> &p, code_blockt &block)
    {
      const exprt map = make_map(type, location, block);
      block.add(code_frontend_assignt{at(p[1], type), map});
      block.add(code_frontend_returnt{});
    });
  // The results of an intrinsic with several results.
  const auto return_tuple =
    [](const exprt::operandst &values, const typet &result, code_blockt &block)
  {
    const auto &components = to_struct_type(result).components();
    exprt::operandst operands;
    for(std::size_t i = 0; i < values.size(); ++i)
      operands.push_back(
        typecast_exprt::conditional_cast(values[i], components[i].type()));
    block.add(code_frontend_returnt{struct_exprt{std::move(operands), result}});
  };
  const auto result_of = [&](const irep_idt &id)
  { return to_code_type(ns.lookup(id).type).return_type(); };
  const pointer_typet void_pointer{empty_typet{}, config.ansi_c.pointer_width};
  // sliceHeader(t, p) (data unsafe.Pointer, len, cap int): of slice *p.
  generate(
    "go::reflect.sliceHeader",
    [](const typet &type) { return is_go_slice(type); },
    [&](const typet &type, const std::vector<exprt> &p, code_blockt &block)
    {
      const struct_typet &slice = to_struct_type(type);
      const exprt s = at(p[1], type);
      return_tuple(
        {typecast_exprt{
           member_exprt{s, slice.get_component("data")}, void_pointer},
         member_exprt{s, slice.get_component("len")},
         member_exprt{s, slice.get_component("cap")}},
        result_of("go::reflect.sliceHeader"),
        block);
    });
  // setSlice(t, p, data, len, cap): *p = the slice of these.
  generate(
    "go::reflect.setSlice",
    [](const typet &type) { return is_go_slice(type); },
    [&](const typet &type, const std::vector<exprt> &p, code_blockt &block)
    {
      const struct_typet &slice = to_struct_type(type);
      block.add(code_frontend_assignt{
        at(p[1], type),
        struct_exprt{
          {typecast_exprt{p[2], slice.get_component("data").type()},
           typecast_exprt::conditional_cast(
             p[3], slice.get_component("len").type()),
           typecast_exprt::conditional_cast(
             p[4], slice.get_component("cap").type())},
          type}});
      block.add(code_frontend_returnt{});
    });
  // mapHeader(t, p) (keys, vals unsafe.Pointer, len int): of map *p (nil
  // pointers and 0 for nil maps).
  generate(
    "go::reflect.mapHeader",
    is_map,
    [&](const typet &type, const std::vector<exprt> &p, code_blockt &block)
    {
      const struct_typet &header =
        to_struct_type(to_pointer_type(type).base_type());
      const symbol_exprt map = new_temporary(type, location, block);
      block.add(code_frontend_assignt{map, at(p[1], type)});
      const auto field = [&](const char *name) {
        return member_exprt{dereference_exprt{map}, header.get_component(name)};
      };
      code_blockt nil;
      return_tuple(
        {null_pointer_exprt{void_pointer},
         null_pointer_exprt{void_pointer},
         from_integer(0, field("len").type())},
        result_of("go::reflect.mapHeader"),
        nil);
      block.add(code_ifthenelset{
        equal_exprt{map, null_pointer_exprt{to_pointer_type(type)}},
        std::move(nil)});
      return_tuple(
        {typecast_exprt{field("keys"), void_pointer},
         typecast_exprt{field("vals"), void_pointer},
         field("len")},
        result_of("go::reflect.mapHeader"),
        block);
    });
  // chanLenCap(t, p) (len, cap int): of channel *p (0 for nil).
  generate(
    "go::reflect.chanLenCap",
    [](const typet &type) { return is_go_chan(type); },
    [&](const typet &type, const std::vector<exprt> &p, code_blockt &block)
    {
      const struct_typet &header =
        to_struct_type(to_pointer_type(type).base_type());
      const symbol_exprt chan = new_temporary(type, location, block);
      block.add(code_frontend_assignt{chan, at(p[1], type)});
      const typet &int_type = header.get_component("cap").type();
      code_blockt nil;
      return_tuple(
        {from_integer(0, int_type), from_integer(0, int_type)},
        result_of("go::reflect.chanLenCap"),
        nil);
      block.add(code_ifthenelset{
        equal_exprt{chan, null_pointer_exprt{to_pointer_type(type)}},
        std::move(nil)});
      return_tuple(
        {member_exprt{dereference_exprt{chan}, header.get_component("count")},
         member_exprt{dereference_exprt{chan}, header.get_component("cap")}},
        result_of("go::reflect.chanLenCap"),
        block);
    });
  // pointerAt(t, p) unsafe.Pointer: the pointer that value *p is (of
  // pointer, map, channel, unsafe.Pointer and function types; the code
  // of function values).
  const auto is_pointer_like = [](const typet &type)
  { return type.id() == ID_pointer || is_func_value(type); };
  generate(
    "go::reflect.pointerAt",
    is_pointer_like,
    [&](const typet &type, const std::vector<exprt> &p, code_blockt &block)
    {
      const exprt value = at(p[1], type);
      const exprt pointer =
        type.id() == ID_pointer
          ? value
          : exprt{
              member_exprt{value, to_struct_type(type).get_component("code")}};
      return_value(
        typecast_exprt{pointer, void_pointer},
        result_of("go::reflect.pointerAt"),
        block);
    });
  // setPointer(t, p, q): *p = q, for pointer types.
  generate(
    "go::reflect.setPointer",
    [](const typet &type) { return type.id() == ID_pointer; },
    [&](const typet &type, const std::vector<exprt> &p, code_blockt &block)
    {
      block.add(
        code_frontend_assignt{at(p[1], type), typecast_exprt{p[2], type}});
      block.add(code_frontend_returnt{});
    });
  // call(t, fn, args, results): calls the function value *fn of type t
  // with the arguments *args[i], storing the results in *results[j].
  // callMethod(t, i, recv, args, results): calls method i (in the order
  // of the descriptor's methods) of the value *recv of type t.
  const auto pointer_at = [&](const exprt &slice, std::size_t i)
  {
    const struct_typet &slice_type =
      to_struct_type(go_follow(ns, slice.type()));
    return dereference_exprt{plus_exprt{
      member_exprt{slice, slice_type.get_component("data")},
      from_integer(i, c_index_type())}};
  };
  // Code calling \p callee with the arguments from \p args, as the
  // parameters of \p code require, and storing its results.
  const auto call_and_store =
    [&](
      const std::function<exprt(exprt::operandst)> &callee,
      const code_typet::parameterst &parameters,
      const typet &return_type,
      const exprt &args,
      const exprt &results,
      code_blockt &block)
  {
    exprt::operandst arguments;
    for(std::size_t i = 0; i < parameters.size(); ++i)
      arguments.push_back(at(pointer_at(args, i), parameters[i].type()));
    exprt call = callee(std::move(arguments));
    if(return_type.id() == ID_empty)
      block.add(code_expressiont{std::move(call)});
    else if(is_tuple(return_type))
    {
      const symbol_exprt tmp = new_temporary(return_type, location, block);
      block.add(code_frontend_assignt{tmp, std::move(call)});
      const auto &components = to_struct_type(return_type).components();
      for(std::size_t j = 0; j < components.size(); ++j)
      {
        block.add(code_frontend_assignt{
          at(pointer_at(results, j), components[j].type()),
          member_exprt{tmp, components[j]}});
      }
    }
    else
    {
      block.add(code_frontend_assignt{
        at(pointer_at(results, 0), return_type), std::move(call)});
    }
    block.add(code_frontend_returnt{});
  };
  // Only if the program calls functions through reflect (these bodies
  // call every function type and method of the described types).
  if(!uses("Value.Call") && !uses("Value.CallSlice"))
  {
    for(const char *id : {"go::reflect.call", "go::reflect.callMethod"})
    {
      symbolt *function = symbol_table.get_writeable(id);
      if(function != nullptr && function->value.is_nil())
      {
        function->value = unsupported_body(
          function->location, "reflect: Value.Call (not used)");
      }
    }
  }
  generate(
    "go::reflect.call",
    [&](const typet &type) { return is_func_value(go_follow(ns, type)); },
    [&](const typet &type, const std::vector<exprt> &p, code_blockt &block)
    {
      const symbol_exprt value = new_temporary(type, location, block);
      block.add(code_frontend_assignt{value, at(p[1], type)});
      const auto &components = to_struct_type(go_follow(ns, type)).components();
      code_typet code =
        to_code_type(to_pointer_type(components[0].type()).base_type());
      code.parameters().erase(code.parameters().begin()); // $env
      const member_exprt code_pointer{value, components[0]};
      const member_exprt env{value, components[1]};
      add_panic(
        block,
        notequal_exprt{
          code_pointer,
          null_pointer_exprt{to_pointer_type(code_pointer.type())}},
        "nil-dereference",
        "runtime error: invalid memory address or nil pointer dereference",
        location);
      call_and_store(
        [&](exprt::operandst arguments)
        {
          arguments.insert(arguments.begin(), env);
          return side_effect_expr_function_callt{
            dereference_exprt{code_pointer},
            std::move(arguments),
            code.return_type(),
            location};
        },
        code.parameters(),
        code.return_type(),
        p[2],
        p[3],
        block);
    });
  if(symbolt *function = symbol_table.get_writeable("go::reflect.callMethod"))
  {
    if(function->value.is_nil())
    {
      const std::vector<exprt> params = parameter_symbols(*function);
      const typet result = to_code_type(function->type).return_type();
      function_statet outer = std::move(state);
      state = function_statet{};
      state.function_id = function->name;
      code_blockt body;
      for(const auto &t : described)
      {
        if(is_go_interface(t.second))
          continue;
        const irept &entry =
          parse_tree.type_entry(type_converter.type_index(t.first));
        const auto methods = sorted_methods(go_list(entry, "mset"));
        code_blockt type_block;
        for(std::size_t j = 0; j < methods.size(); ++j)
        {
          const irept &m = *methods[j];
          code_blockt block;
          std::vector<std::string> messages;
          unsupported_in_body = &messages;
          const exprt receiver = method_receiver(
            at(params[2], t.second),
            m.find("path"),
            m.get("obj"),
            location,
            block);
          unsupported_in_body = nullptr;
          if(receiver.is_nil())
            continue;
          const code_typet code =
            to_code_type(type_converter.signature(m.get("type")));
          call_and_store(
            [&](exprt::operandst arguments)
            {
              return call_method(
                receiver,
                m.get("obj"),
                m.get("type"),
                std::move(arguments),
                location);
            },
            code.parameters(),
            code.return_type(),
            params[3],
            params[4],
            block);
          type_block.add(code_ifthenelset{
            equal_exprt{params[1], from_integer(j, params[1].type())},
            std::move(block)});
        }
        if(type_block.statements().empty())
          continue;
        body.add(code_ifthenelset{
          equal_exprt{params[0], from_integer(t.first, params[0].type())},
          std::move(type_block)});
      }
      body.append(unsupported_body(
        function->location,
        "reflect: a call of a method that the model does not describe"));
      end_generated_function(body, result);
      state = std::move(outer);
      function->value = std::move(body);
    }
  }
  // makeSlice(t, s, len, cap): *s = make(T, len, cap).
  generate(
    "go::reflect.makeSlice",
    [](const typet &type) { return is_go_slice(type); },
    [&](const typet &type, const std::vector<exprt> &p, code_blockt &block)
    {
      const struct_typet &slice = to_struct_type(type);
      const typet &element =
        to_pointer_type(slice.get_component("data").type()).base_type();
      const exprt data =
        allocate_elements(element, p[3], true, location, block);
      block.add(code_frontend_assignt{
        at(p[1], type),
        struct_exprt{
          {typecast_exprt::conditional_cast(
             data, slice.get_component("data").type()),
           typecast_exprt::conditional_cast(
             p[2], slice.get_component("len").type()),
           typecast_exprt::conditional_cast(
             p[3], slice.get_component("cap").type())},
          type}});
      block.add(code_frontend_returnt{});
    });
}
