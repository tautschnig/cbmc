/*******************************************************************\

Module: Symbolic Execution: value sets of shared objects

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// Sound value sets for objects shared between threads

#include "shared_value_summary.h"

#include <algorithm>

void shared_value_summaryt::begin_run()
{
  ++run_count;
  active = false;
  recorded.clear();
}

bool shared_value_summaryt::shared(const irep_idt &identifier) const
{
  // L1 identifiers are the object's name, possibly with L0/L1 suffixes
  // ("!thread@frame") and, for field-sensitive symbols, the fields and
  // elements ("..field", "[[index]]").
  std::string name = id2string(identifier);
  std::size_t end = name.find_first_of("!@#");
  end = std::min(end, name.find(".."));
  end = std::min(end, name.find("[["));
  if(end != std::string::npos)
    name.erase(end);
  return is_shared(name);
}

void shared_value_summaryt::record(
  const value_sett &value_set,
  const irep_idt &index,
  const value_sett::entryt &entry,
  const value_sett::object_mapt &values)
{
  auto it = recorded.find(index);
  if(it == recorded.end())
  {
    value_sett::entryt copy{entry.identifier, entry.suffix};
    copy.object_map = values;
    recorded.emplace(index, std::move(copy));
  }
  else
    value_set.make_union(it->second.object_map, values);
}

void shared_value_summaryt::spawn(value_sett &value_set)
{
  active = true;
  // The values shared entries have now: threads (executed later) may still
  // read them.
  value_sett::valuest::viewt view;
  value_set.values.get_view(view);
  for(const auto &pair : view)
  {
    if(shared(pair.second.identifier))
      record(value_set, pair.first, pair.second, pair.second.object_map);
  }
  // What any thread may write to them.
  for(const auto &pair : summary)
  {
    const value_sett::entryt *existing = value_set.find_entry(pair.first);
    if(existing == nullptr)
      value_set.values.insert(pair.first, pair.second);
    else if(value_set.make_union_would_change(
              existing->object_map, pair.second.object_map))
    {
      value_set.values.update(
        pair.first,
        [&](value_sett::entryt &entry)
        { value_set.make_union(entry.object_map, pair.second.object_map); });
    }
  }
}

const value_sett::object_mapt *shared_value_summaryt::updated(
  const value_sett &value_set,
  const irep_idt &index,
  const value_sett::entryt &entry,
  const value_sett::object_mapt &values,
  bool add_to_sets)
{
  if(!active || !shared(entry.identifier))
    return nullptr;
  record(value_set, index, entry, values);
  // Another thread may read any value written to the entry.
  const auto it = summary.find(index);
  return it == summary.end() ? nullptr : &it->second.object_map;
}

bool shared_value_summaryt::grew(const value_sett &value_set)
{
  bool changed = false;
  for(auto &pair : recorded)
  {
    auto it = summary.find(pair.first);
    if(it == summary.end())
    {
      summary.emplace(pair.first, std::move(pair.second));
      changed = true;
    }
    else if(value_set.make_union(it->second.object_map, pair.second.object_map))
    {
      changed = true;
    }
  }
  recorded.clear();
  return changed;
}
