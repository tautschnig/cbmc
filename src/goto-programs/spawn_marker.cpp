#include "spawn_marker.h"

#include <iostream>
#include <algorithm>
#include <set>

spawn_markert::spawn_markert(cfgt &cfg, namespacet &ns)
  :cfg(cfg), ns(ns) {}

void spawn_markert::operator()()
{
  /* Algorithm:
   * 1. Find nodes containing pthread_create(..., func_name)
   * 2. From such nodes, find cfg-subsequent GOTO nodes whose guard
   *    is `start_routine == func_name'
   * 3. Go to the target of such nodes; that target is the spawn point
   *    of the thread. That target, plus all cfg-subsequent nodes
   *    until END OF FUNCTION, get their instructiont::spawn_point
   *    members pointing to the original pthread_create node.
   */

  for(unsigned i = 0; i < cfg.size(); i++)
  {
    targett ins = cfg[i].PC;

    if(!ins->is_function_call())
      continue;

    std::string code =
      from_expr(ns, "", ins->code);
    if(code.find("pthread_create", 0) != 0)
      continue;

    std::list<unsigned> worklist;
    std::list<unsigned> visited;
    cfgt::edgest edgelist = cfg.out(i);
    cfgt::edgest::iterator it = edgelist.begin();
    for(; it != edgelist.end(); it++)
      worklist.push_front(it->first);

    std::set<unsigned> marked;

    while(!worklist.empty())
    {
      unsigned curr = worklist.front();
      worklist.pop_front();
      visited.push_back(curr);

      if(marked.find(curr) != marked.end())
        continue;
      marked.insert(curr);

      targett work_item = cfg[curr].PC;

      if(work_item->is_goto()
      && from_expr(ns, "", work_item->guard)
      == "start_routine == " + pthread_target(ins))
      {
        start_routine(i, curr);
      }
      else
      {
        cfgt::edgest edgelist = cfg.out(curr);
        cfgt::edgest::iterator it = edgelist.begin();
        for(; it != edgelist.end(); it++)
          if(std::find(visited.begin(), visited.end(), it->first)
          == visited.end())
            worklist.push_front(it->first);
      }
    }
  }
}

void spawn_markert::start_routine(
    unsigned spawn_node,
    unsigned target_node)
{
  targett ins = cfg[target_node].PC;

  std::list<unsigned> worklist;
  std::list<unsigned> visited;
  cfgt::edgest edges = cfg.out(target_node);
  cfgt::edgest::iterator it = edges.begin();
  for(; it != edges.end(); it++)
    if(cfg[it->first].PC == ins->get_target())
      worklist.push_front(it->first);

  // the start_routine node should have only a single target, namely
  // the beginning of the spawned function. This beginning of the
  // spawned function is what is now in the worklist.
  assert(worklist.size() == 1);

  while(!worklist.empty())
  {
    unsigned curr = worklist.front();
    worklist.pop_front();
    visited.push_back(curr);

    targett work_item = cfg[curr].PC;

    if(work_item->is_end_thread())
      continue;

    cfg.add_spawn_edge(curr, spawn_node);

    cfgt::edgest edges = cfg.out(curr);
    cfgt::edgest::iterator it = edges.begin();
    for(; it != edges.end(); it++)
      if(std::find(visited.begin(), visited.end(), it->first)
      == visited.end())
        worklist.push_front(it->first);
  }
}

std::string spawn_markert::pthread_target(
    targett &pthread_ins)
{
  irept::named_subt sub =
    pthread_ins->code.    // Function call
    op2().                // Parameters
    op2().                // Function pointer param
    op0().
    get_named_sub();
  irept irept_id = sub["identifier"];

  // This will be e.g. ``c::func''
  std::string identifier = irept_id.pretty();

  // Get rid of everything before the actual function name
  unsigned colons = 0;
  while(colons < 2)
  {
    if(identifier[0] == ':')
      colons++;

    identifier = identifier.substr(1, std::string::npos);
  }

  // There should be no more colons in the identifier.
  assert(identifier.find(":") == std::string::npos);

  return identifier;
}

inline unsigned spawn_markert::loc_of(unsigned node)
{
  return cfg[node].PC->location_number;
}
