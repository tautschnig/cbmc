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
#include <sstream>

/// Read the strings of the JSON array \p json into \p values.
/// \return true on error
static bool strings_from_json(const jsont &json, std::set<std::string> &values)
{
  if(!json.is_array())
    return true;
  for(const auto &value : to_json_array(json))
  {
    if(!value.is_string())
      return true;
    values.insert(value.value);
  }
  return false;
}

/// Read \p resolutions from \p json, read from \p filename.
static bool resolutions_from_json(
  const jsont &json,
  const std::string &filename,
  message_handlert &message_handler,
  dynamic_resolutionst &resolutions)
{
  messaget log{message_handler};
  if(!json.is_object())
  {
    log.error() << filename
                << ": dynamic resolutions must be a JSON object that maps "
                   "site identifiers to their candidates"
                << messaget::eom;
    return true;
  }
  for(const auto &entry : to_json_object(json))
  {
    auto &resolution = resolutions[entry.first];
    bool error = false;
    if(entry.second.is_array())
      error = strings_from_json(entry.second, resolution.values);
    else if(entry.second.is_object())
    {
      for(const auto &field : to_json_object(entry.second))
      {
        if(field.first == "values")
          error |= strings_from_json(field.second, resolution.values);
        else if(field.first == "containing" && field.second.is_string())
          resolution.containing = field.second.value;
        else
          error = true;
      }
    }
    else
      error = true;
    if(error)
    {
      log.error() << filename << ": the candidates of " << entry.first
                  << " must be an array of strings, or an object with an "
                     "array of strings \"values\" and a string "
                     "\"containing\""
                  << messaget::eom;
      return true;
    }
  }
  return false;
}

bool join_dynamic_resolution(
  dynamic_resolutiont &resolution,
  const dynamic_resolutiont &other)
{
  if(other.containing.has_value())
  {
    if(
      resolution.containing.has_value() &&
      *resolution.containing != *other.containing)
    {
      return false;
    }
    resolution.containing = other.containing;
  }
  resolution.values.insert(other.values.begin(), other.values.end());
  if(resolution.containing.has_value())
  {
    for(auto it = resolution.values.begin(); it != resolution.values.end();)
    {
      it = it->find(*resolution.containing) != std::string::npos
             ? resolution.values.erase(it)
             : std::next(it);
    }
  }
  return true;
}

bool read_dynamic_resolutions(
  const std::string &filename,
  message_handlert &message_handler,
  dynamic_resolutionst &resolutions)
{
  jsont json;
  if(parse_json(filename, message_handler, json))
  {
    messaget log{message_handler};
    log.error() << "failed to read dynamic resolutions from " << filename
                << messaget::eom;
    return true;
  }
  return resolutions_from_json(json, filename, message_handler, resolutions);
}

bool parse_dynamic_resolutions(
  const std::string &text,
  message_handlert &message_handler,
  dynamic_resolutionst &resolutions)
{
  std::istringstream in{text};
  jsont json;
  if(parse_json(in, "<dynamic resolutions>", message_handler, json))
    return true;
  return resolutions_from_json(
    json, "<dynamic resolutions>", message_handler, resolutions);
}

std::string dynamic_resolutions_to_json(const dynamic_resolutionst &resolutions)
{
  json_objectt json;
  for(const auto &site : resolutions)
  {
    json_arrayt values;
    for(const auto &value : site.second.values)
      values.push_back(json_stringt{value});
    if(!site.second.containing.has_value())
      json[id2string(site.first)] = std::move(values);
    else
    {
      json[id2string(site.first)] = json_objectt{
        {"values", std::move(values)},
        {"containing", json_stringt{*site.second.containing}}};
    }
  }
  std::ostringstream out;
  out << json;
  return out.str();
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
  // The location of each call, rather than of the symbol, which is that of
  // the first site.
  symbol_exprt function =
    symbol_table.lookup_ref(DYNAMIC_SITE_FUNCTION).symbol_expr();
  function.add_source_location() = location;
  code_function_callt call{
    function, {constant_exprt{site_id, string_typet{}}, value}};
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
  const dynamic_resolutionst *resolutions,
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

  // The candidates of a site, or nothing if there are no resolutions or
  // the site has none (any value).
  const auto candidates =
    [resolutions](const irep_idt &site) -> const dynamic_resolutiont *
  {
    if(resolutions == nullptr)
      return nullptr;
    const auto found = resolutions->find(site);
    return found == resolutions->end() ? nullptr : &found->second;
  };

  messaget log{message_handler};
  switch(message_handler.get_ui())
  {
  case ui_message_handlert::uit::PLAIN:
    for(const auto &site : sites)
    {
      log.result() << site.first << ": " << site.second;
      if(resolutions != nullptr)
      {
        const auto *found = candidates(site.first);
        if(found == nullptr)
          log.result() << ": unresolved";
        else
        {
          log.result() << ": ";
          if(!found->values.empty() || !found->containing.has_value())
          {
            log.result() << '{';
            bool first = true;
            for(const auto &candidate : found->values)
            {
              log.result() << (first ? " \"" : ", \"") << candidate << '"';
              first = false;
            }
            log.result() << " }";
            if(found->containing.has_value())
              log.result() << " or ";
          }
          if(found->containing.has_value())
            log.result() << "containing \"" << *found->containing << '"';
        }
      }
      log.result() << messaget::eom;
    }
    break;
  case ui_message_handlert::uit::JSON_UI:
  {
    json_arrayt json_sites;
    for(const auto &site : sites)
    {
      json_objectt json_site{
        {"site", json_stringt{site.first}},
        {"sourceLocation", json(site.second)}};
      if(resolutions != nullptr)
      {
        const auto *found = candidates(site.first);
        if(found == nullptr)
          json_site["resolved"] = jsont::json_boolean(false);
        else
        {
          json_site["resolved"] = jsont::json_boolean(true);
          json_arrayt json_candidates;
          for(const auto &candidate : found->values)
            json_candidates.push_back(json_stringt{candidate});
          json_site["candidates"] = std::move(json_candidates);
          if(found->containing.has_value())
            json_site["containing"] = json_stringt{*found->containing};
        }
      }
      json_sites.push_back(std::move(json_site));
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
      if(resolutions != nullptr)
      {
        const auto *found = candidates(site.first);
        xml_site.set_attribute_bool("resolved", found != nullptr);
        if(found != nullptr)
        {
          for(const auto &candidate : found->values)
            xml_site.new_element("candidate").data = candidate;
          if(found->containing.has_value())
            xml_site.new_element("containing").data = *found->containing;
        }
      }
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
