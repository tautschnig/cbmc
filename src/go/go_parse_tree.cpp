/*******************************************************************\

Module: Go front-end: in-memory Go AST

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// Loading the helper's JSON into the in-memory Go AST

#include "go_parse_tree.h"

#include <util/json.h>
#include <util/message.h>

#include <ostream>

namespace
{
/// Converts JSON values into the generic irept form described in
/// go_parse_tree.h.
class json_loadert
{
public:
  explicit json_loadert(const std::vector<std::string> &files) : files(files)
  {
  }

  /// A JSON object becomes a node (if it has "_k") or a plain record.
  irept object(const jsont &json) const
  {
    const auto &obj = to_json_object(json);
    const auto kind = obj.find("_k");
    irept result{kind == obj.end() ? irep_idt{"record"} : kind->second.value};
    for(const auto &entry : obj)
    {
      if(entry.first == "_k")
        continue;
      if(entry.first == "_p")
      {
        result.add(ID_C_source_location) = location(entry.second);
        continue;
      }
      store(result, entry.first, entry.second);
    }
    return result;
  }

private:
  const std::vector<std::string> &files;

  void store(irept &dest, const std::string &field, const jsont &value) const
  {
    if(value.is_object())
      dest.add(field) = object(value);
    else if(value.is_array())
      dest.add(field) = list(value);
    else if(value.is_true())
      dest.set(field, "1");
    else if(value.is_false() || value.is_null())
      return;
    else
      dest.set(field, value.value); // string or number
  }

  irept list(const jsont &json) const
  {
    irept result{"list"};
    for(const auto &element : to_json_array(json))
    {
      if(element.is_object())
        result.get_sub().push_back(object(element));
      else if(element.is_array())
        result.get_sub().push_back(list(element));
      else
        result.get_sub().push_back(irept{element.value});
    }
    return result;
  }

  /// "_p": [file index, line, column]
  source_locationt location(const jsont &json) const
  {
    source_locationt loc;
    const auto &pos = to_json_array(json);
    if(pos.size() != 3)
      return loc;
    auto it = pos.begin();
    const std::size_t file = std::stoul(it->value);
    ++it;
    if(file < files.size())
      loc.set_file(files[file]);
    loc.set_line(it->value);
    ++it;
    loc.set_column(it->value);
    return loc;
  }
};
} // namespace

bool go_parse_treet::load_json(
  const jsont &json,
  message_handlert &message_handler)
{
  messaget log{message_handler};
  clear();
  if(!json.is_object() || json["version"].value != "1")
  {
    log.error() << "unsupported Go helper output (expected schema version 1)"
                << messaget::eom;
    return true;
  }

  go_version = json["go_version"].value;
  goarch = json["goarch"].value;
  package = json["package"].value;
  for(const auto &f : to_json_array(json["files"]))
    files.push_back(f.value);

  json_loadert loader{files};
  for(const auto &ast : to_json_array(json["asts"]))
    asts.push_back(loader.object(ast));
  for(const auto &t : to_json_array(json["types"]))
    types.push_back(loader.object(t));
  for(const auto &o : to_json_object(json["objects"]))
    objects.emplace(o.first, loader.object(o.second));
  for(const auto &init : to_json_array(json["init_order"]))
  {
    initializert entry;
    for(const auto &id : to_json_array(init["lhs"]))
      entry.lhs.push_back(id.value);
    entry.rhs = loader.object(init["rhs"]);
    init_order.push_back(std::move(entry));
  }
  return false;
}

void go_parse_treet::clear()
{
  go_version.clear();
  goarch.clear();
  package.clear();
  files.clear();
  asts.clear();
  types.clear();
  objects.clear();
  init_order.clear();
}

void go_parse_treet::output(std::ostream &out) const
{
  out << "Go package " << package << " (" << go_version << ", " << goarch
      << ")\n";
  for(const auto &ast : asts)
    out << ast.pretty() << '\n';
}

const irept &go_parse_treet::type_entry(const irep_idt &index) const
{
  if(index.empty())
    return get_nil_irep();
  const std::size_t i = std::stoul(id2string(index));
  return i < types.size() ? types[i] : get_nil_irep();
}

const irept &go_parse_treet::object(const irep_idt &id) const
{
  const auto it = objects.find(id);
  return it == objects.end() ? get_nil_irep() : it->second;
}
