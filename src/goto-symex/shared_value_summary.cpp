/*******************************************************************\

Module: Symbolic Execution: value sets of shared objects

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// Sound value sets for objects shared between threads

#include "shared_value_summary.h"

#include <util/cprover_prefix.h>
#include <util/std_expr.h>

#include <algorithm>

void shared_value_summaryt::begin_run()
{
  ++run_count;
  active = false;
  recorded.clear();
  young.clear();
  initialising.clear();
}

irep_idt shared_value_summaryt::object_of(const irep_idt &identifier)
{
  // L1 identifiers are the object's name, possibly with L0/L1 suffixes
  // ("!thread@frame") and, for field-sensitive symbols, the fields and
  // elements ("..field", "[[index]]").
  const std::string &name = id2string(identifier);
  std::size_t end = name.find_first_of("!@#");
  end = std::min(end, name.find(".."));
  end = std::min(end, name.find("[["));
  if(end == std::string::npos)
    return identifier;
  return name.substr(0, end);
}

bool shared_value_summaryt::shared(const irep_idt &identifier) const
{
  return is_shared(object_of(identifier));
}

/// Whether \p object is a pointer of the C library and instrumentation
/// that records an object (for pointer and memory-leak checks), and is only
/// compared, never dereferenced.
static bool is_tracking_pointer(const irep_idt &object)
{
  static const std::set<irep_idt> tracking{
    CPROVER_PREFIX "alloca_object",
    CPROVER_PREFIX "dead_object",
    CPROVER_PREFIX "deallocated",
    CPROVER_PREFIX "memory_leak",
    CPROVER_PREFIX "new_object"};
  return tracking.count(object) != 0;
}

void shared_value_summaryt::begin_allocation(const irep_idt &object)
{
  if(!active)
    return;
  young[object];
  initialising = object;
}

void shared_value_summaryt::publish(
  const value_sett &value_set,
  const value_sett::object_mapt &values)
{
  std::vector<irep_idt> work;
  std::set<irep_idt> seen;
  const auto add_objects = [&](const value_sett::object_mapt &map)
  {
    for(const auto &pair : map.read())
    {
      const exprt &object = value_set.object_numbering[pair.first];
      if(object.id() != ID_symbol)
        continue;
      const irep_idt &id = to_symbol_expr(object).get_identifier();
      if(young.count(id) != 0 && seen.insert(id).second)
        work.push_back(id);
    }
  };
  add_objects(values);
  while(!work.empty())
  {
    const irep_idt object = work.back();
    work.pop_back();
    for(const auto &index : young.at(object))
    {
      const value_sett::entryt *entry = value_set.find_entry(index);
      if(entry == nullptr)
        continue;
      record(value_set, index, *entry, entry->object_map);
      add_objects(entry->object_map);
    }
  }
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
  const irep_idt object = object_of(entry.identifier);
  auto young_it = young.find(object);
  if(young_it != young.end())
    young_it->second.insert(index);
  // The initial value of a new object is recorded when it is published.
  if(object != initialising)
    record(value_set, index, entry, values);
  // Other threads can read the objects that values point to, unless the
  // entry is one that the library and instrumentation only compare.
  if(!is_tracking_pointer(object))
    publish(value_set, values);
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
