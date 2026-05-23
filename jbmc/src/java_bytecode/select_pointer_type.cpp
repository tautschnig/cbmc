/*******************************************************************\

Module: Java Bytecode Language Conversion

Author: Diffblue Ltd.

\*******************************************************************/

/// \file
/// Handle selection of correct pointer type (for example changing abstract
/// classes to concrete versions).

#include "select_pointer_type.h"

#include <util/namespace.h>
#include <util/std_code.h>
#include <util/std_types.h>
#include <util/symbol_table_base.h>

#include <goto-programs/class_hierarchy.h>

#include "generic_parameter_specialization_map.h"
#include "java_types.h"

pointer_typet select_pointer_typet::convert_pointer_type(
  const pointer_typet &pointer_type,
  const generic_parameter_specialization_mapt
    &generic_parameter_specialization_map,
  const namespacet &) const
{
  return specialize_generics(
    pointer_type, generic_parameter_specialization_map);
}

pointer_typet select_pointer_typet::specialize_generics(
  const pointer_typet &pointer_type,
  const generic_parameter_specialization_mapt
    &generic_parameter_specialization_map) const
{
  auto parameter = type_try_dynamic_cast<java_generic_parametert>(pointer_type);
  if(parameter != nullptr)
  {
    irep_idt parameter_name = parameter->get_name();

    // Make a local copy of the specialization map to unwind
    generic_parameter_specialization_mapt spec_map_copy =
      generic_parameter_specialization_map;
    while(true)
    {
      const std::optional<reference_typet> specialization =
        spec_map_copy.pop(parameter_name);
      if(!specialization)
      {
        // This means that the generic pointer_type has not been specialized
        // in the current context (e.g., the method under test is generic);
        // we return the pointer_type itself which is a pointer to its upper
        // bound
        return pointer_type;
      }

      if(!is_java_generic_parameter(*specialization))
        return *specialization;
      parameter_name = to_java_generic_parameter(*specialization).get_name();
    }
  }

  auto base_type =
    type_try_dynamic_cast<struct_tag_typet>(pointer_type.base_type());
  if(base_type != nullptr && is_java_array_tag(base_type->get_identifier()))
  {
    // if the pointer is an array, recursively specialize its element type
    const auto *array_element_type =
      type_try_dynamic_cast<pointer_typet>(java_array_element_type(*base_type));
    if(array_element_type == nullptr)
      return pointer_type;

    const pointer_typet &new_array_type = specialize_generics(
      *array_element_type, generic_parameter_specialization_map);

    pointer_typet replacement_array_type = java_array_type('a');
    replacement_array_type.base_type().set(ID_element_type, new_array_type);
    return replacement_array_type;
  }

  return pointer_type;
}

std::set<struct_tag_typet>
select_pointer_typet::get_parameter_alternative_types(
  const irep_idt &function_name,
  const irep_idt &parameter_name,
  const namespacet &ns) const
{
  // §5.3 / sealed-init: if the parameter's static type is a
  // sealed class/interface, inject the permitted-subclasses
  // list as alternative concrete types for the harness's
  // nondet switch. Without this, JBMC's lazy-init allocates an
  // object whose @class_identifier is left as the abstract
  // sealed-interface tag, and any sealed-pattern-match
  // downstream falls into the synthetic MatchException default
  // that javac emits to satisfy the JVM verifier (the permits
  // clause proves the default unreachable, but only if the
  // runtime tag is pinned to one of the permits — that's what
  // we do here).
  //
  // Mechanism: locate the parameter symbol via the function
  // symbol's parameter list, follow the parameter's pointer
  // type to the underlying struct_tag_typet, look up the class
  // on the symbol table, and read the
  // ID_permitted_subclasses string set by
  // java_bytecode_convert_class.cpp.
  //
  // Limitation: this only fires for entry-point parameters.
  // Field-lazy-init for sealed-typed fields inside
  // nondet-allocated classes is a separate code path
  // (gen_nondet_pointer_init in java_object_factory.cpp) and
  // doesn't yet consult the permits list. For sealed types
  // exposed via a record field (e.g.,
  // `record OuterA(Inner inner)` where Inner is sealed),
  // verification still hits the MatchException default. Filed
  // as future work.
  const symbolt *function_symbol = ns.get_symbol_table().lookup(function_name);
  if(function_symbol == nullptr || function_symbol->type.id() != ID_code)
    return {};
  const code_typet &code_type = to_code_type(function_symbol->type);
  for(const auto &param : code_type.parameters())
  {
    if(param.get_identifier() != parameter_name)
      continue;
    if(param.type().id() != ID_pointer)
      return {};
    const typet &subtype = to_pointer_type(param.type()).base_type();
    if(subtype.id() != ID_struct_tag)
      return {};
    const irep_idt &class_id = to_struct_tag_type(subtype).get_identifier();
    const symbolt *class_symbol = ns.get_symbol_table().lookup(class_id);
    if(class_symbol == nullptr)
      return {};
    const irep_idt permits_str =
      class_symbol->type.get(ID_permitted_subclasses);
    if(permits_str.empty())
    {
      // No sealed-permits annotation. If the parameter's static
      // type is an interface or abstract class, fall back to
      // walking the class hierarchy and returning all concrete
      // (non-interface, non-abstract) descendants. Without this,
      // the entry-point harness would allocate an object whose
      // @class_identifier is the interface tag itself, and any
      // virtual dispatch would fail to resolve to a concrete
      // implementation.
      if(class_symbol->type.id() != ID_struct)
        return {};
      const auto &class_type = to_java_class_type(class_symbol->type);
      const bool is_interface = class_type.get_interface();
      const bool is_abstract = class_type.get_abstract();
      if(!is_interface && !is_abstract)
        return {};

      // Build a class hierarchy on the fly and find concrete
      // descendants. Note: a more elegant solution would be to
      // pass class_hierarchyt through the constructor, but the
      // existing select_pointer_typet has no hierarchy member;
      // building it once per entry-point parameter is acceptable
      // overhead (entry-point code runs once per analysis).
      class_hierarchyt class_hierarchy{ns.get_symbol_table()};
      const auto descendants = class_hierarchy.get_children_trans(class_id);
      std::set<struct_tag_typet> result;
      for(const irep_idt &descendant : descendants)
      {
        const symbolt *desc_sym = ns.get_symbol_table().lookup(descendant);
        if(desc_sym == nullptr || desc_sym->type.id() != ID_struct)
          continue;
        const auto &desc_type = to_java_class_type(desc_sym->type);
        if(desc_type.get_interface() || desc_type.get_abstract())
          continue; // Only concrete classes can be allocated
        result.insert(struct_tag_typet(descendant));
      }
      return result;
    }
    std::set<struct_tag_typet> result;
    const std::string joined = id2string(permits_str);
    std::string current;
    auto try_add = [&](const std::string &name)
    {
      if(name.empty())
        return;
      const std::string permit_name = "java::" + name;
      // Skip permits whose class symbol isn't loaded; nondet-
      // init can't allocate a class it has no symbol for.
      if(ns.get_symbol_table().lookup(permit_name) == nullptr)
        return;
      result.insert(struct_tag_typet(permit_name));
    };
    for(char c : joined)
    {
      if(c == ',')
      {
        try_add(current);
        current.clear();
      }
      else
      {
        current.push_back(c);
      }
    }
    try_add(current);
    return result;
  }
  return {};
}
