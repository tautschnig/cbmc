/*******************************************************************\

Module: Dynamic sites

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// Dynamic sites

#include "dynamic_sites.h"

#include <util/json_irep.h>
#include <util/message.h>
#include <util/std_types.h>
#include <util/symbol_table_base.h>
#include <util/ui_message.h>
#include <util/xml_irep.h>

#include <json/json_parser.h>

#include "goto_model.h"

#include <optional>

bool read_dynamic_resolutions(
  const std::string &filename,
  message_handlert &message_handler,
  dynamic_resolutionst &resolutions)
{
  messaget log{message_handler};
  jsont json;
  if(parse_json(filename, message_handler, json))
  {
    log.error() << "failed to read dynamic resolutions from " << filename
                << messaget::eom;
    return true;
  }
  if(!json.is_object())
  {
    log.error() << filename
                << ": dynamic resolutions must be a JSON object that maps "
                   "site identifiers to arrays of strings"
                << messaget::eom;
    return true;
  }
  for(const auto &entry : to_json_object(json))
  {
    if(!entry.second.is_array())
    {
      log.error() << filename << ": the candidates of " << entry.first
                  << " must be an array of strings" << messaget::eom;
      return true;
    }
    auto &candidates = resolutions[entry.first];
    for(const auto &candidate : to_json_array(entry.second))
    {
      if(!candidate.is_string())
      {
        log.error() << filename << ": the candidates of " << entry.first
                    << " must be strings" << messaget::eom;
        return true;
      }
      candidates.insert(candidate.value);
    }
  }
  return false;
}

code_function_callt dynamic_site_marker(
  const irep_idt &site_id,
  const exprt &value,
  const irep_idt &mode,
  symbol_table_baset &symbol_table,
  const source_locationt &location)
{
  if(!symbol_table.has_symbol(DYNAMIC_SITE_FUNCTION))
  {
    // Named parameters: analyses bind arguments to them.
    code_typet::parametert site{string_typet{}};
    site.set_identifier(DYNAMIC_SITE_FUNCTION "::site");
    site.set_base_name("site");
    code_typet type{{site}, empty_typet{}};
    type.make_ellipsis();
    symbolt parameter{site.get_identifier(), site.type(), mode};
    parameter.base_name = site.get_base_name();
    parameter.is_parameter = true;
    parameter.is_lvalue = true;
    parameter.is_thread_local = true;
    parameter.is_file_local = true;
    parameter.location = location;
    symbol_table.add(parameter);
    symbolt function{DYNAMIC_SITE_FUNCTION, type, mode};
    function.base_name = DYNAMIC_SITE_FUNCTION;
    function.pretty_name = DYNAMIC_SITE_FUNCTION;
    function.location = location;
    symbol_table.add(function);
  }
  code_function_callt call{
    symbol_table.lookup_ref(DYNAMIC_SITE_FUNCTION).symbol_expr(),
    {constant_exprt{site_id, string_typet{}}, value}};
  call.add_source_location() = location;
  return call;
}

/// The identifier of the dynamic site that \p instruction marks, or empty.
static irep_idt marked_site(const goto_programt::instructiont &instruction)
{
  if(!instruction.is_function_call())
    return {};
  const exprt &function = instruction.call_function();
  // Compared as text: making DYNAMIC_SITE_FUNCTION an irep_idt would
  // intern it in every program, which changes the order of identifiers,
  // to which the string refinement solver is sensitive.
  if(
    function.id() != ID_symbol ||
    id2string(to_symbol_expr(function).identifier()) != DYNAMIC_SITE_FUNCTION ||
    instruction.call_arguments().empty() ||
    !instruction.call_arguments().front().is_constant())
  {
    return {};
  }
  return to_constant_expr(instruction.call_arguments().front()).get_value();
}

void show_dynamic_sites(
  const goto_modelt &goto_model,
  ui_message_handlert &message_handler)
{
  // Each site once, although conversion may copy it (into functions,
  // subshells, or branches).
  std::map<irep_idt, source_locationt> sites;
  for(const auto &entry : goto_model.goto_functions.function_map)
  {
    for(const auto &instruction : entry.second.body.instructions)
    {
      const irep_idt site = marked_site(instruction);
      if(!site.empty())
        sites.emplace(site, instruction.source_location());
    }
  }

  messaget log{message_handler};
  switch(message_handler.get_ui())
  {
  case ui_message_handlert::uit::PLAIN:
    for(const auto &site : sites)
    {
      log.result() << site.first << ": " << site.second << messaget::eom;
    }
    break;
  case ui_message_handlert::uit::JSON_UI:
  {
    json_arrayt json_sites;
    for(const auto &site : sites)
    {
      json_sites.push_back(json_objectt{
        {"site", json_stringt{site.first}},
        {"sourceLocation", json(site.second)}});
    }
    log.result() << json_objectt{{"dynamicSites", std::move(json_sites)}};
    log.result() << messaget::eom;
    break;
  }
  case ui_message_handlert::uit::XML_UI:
  {
    xmlt xml_sites{"dynamic-sites"};
    for(const auto &site : sites)
    {
      xmlt &xml_site = xml_sites.new_element("site");
      xml_site.set_attribute("id", id2string(site.first));
      xml_site.new_element(xml(site.second));
    }
    log.result() << xml_sites << messaget::eom;
    break;
  }
  }
}

void remove_dynamic_site_markers(goto_modelt &goto_model)
{
  // The identifier of the function, taken from a marker: interning
  // DYNAMIC_SITE_FUNCTION would change the order of identifiers in every
  // program (see marked_site).
  std::optional<irep_idt> identifier;
  for(auto &entry : goto_model.goto_functions.function_map)
  {
    for(auto &instruction : entry.second.body.instructions)
    {
      if(marked_site(instruction).empty())
        continue;
      identifier = to_symbol_expr(instruction.call_function()).identifier();
      instruction.turn_into_skip();
    }
  }
  if(!identifier.has_value())
    return;
  goto_model.goto_functions.function_map.erase(*identifier);
  for(const auto &parameter :
      to_code_type(goto_model.symbol_table.lookup_ref(*identifier).type)
        .parameters())
  {
    goto_model.symbol_table.remove(parameter.get_identifier());
  }
  goto_model.symbol_table.remove(*identifier);
}
