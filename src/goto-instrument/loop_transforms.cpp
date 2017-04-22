/*******************************************************************\

Module: Perform loop transformations as commonly done in compilers

Author: Michael Tautschnig

Date: April 2016

\*******************************************************************/

#include <goto-programs/goto_functions.h>

#include <analyses/dependence_graph.h>
#include <analyses/natural_loops.h>

#include "loop_transforms.h"

/*******************************************************************\

Function: transform_loops

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

static bool transform_loops(
  goto_programt &goto_program,
  dependence_grapht &dep_graph)
{
  natural_loops_mutablet natural_loops(goto_program);
  if(natural_loops.loop_map.empty())
    return false;

  bool transformed=false;

  typedef std::map<goto_programt::targett, goto_programt::targett>
    loopt;
  loopt loop_map;

  // TODO: copied from goto_program2codet::build_loop_map() - refactor
  for(const auto &loop : natural_loops.loop_map)
  {
    // l_it->first need not be the program-order first instruction in the
    // natural loop, because a natural loop may have multiple entries. But we
    // ignore all those before l_it->first
    // Furthermore the loop may contain code after the source of the actual back
    // edge -- we also ignore such code
    goto_programt::targett loop_start=loop.first;
    goto_programt::targett loop_end=loop_start;
    for(const auto &target : loop.second)
      if(target->is_goto())
      {
        if(target->get_target()==loop_start &&
           target->location_number>loop_end->location_number)
          loop_end=target;
      }

    if(!loop_map.insert(std::make_pair(loop_start, loop_end)).second)
      assert(false);
  }

  // transform each loop
  for(const auto &loop : natural_loops.loop_map)
  {
    goto_programt::targett target=loop.first;
    loopt::const_iterator entry=loop_map.find(target);
    assert(entry!=loop_map.end());
    goto_programt::targett loop_end=entry->second;

    // iterate all statements in each loop
    while(target!=loop_end)
    {
      // load dependence information
      const dependence_grapht::statet &s=dep_graph.get_state(target);
      const dep_graph_domaint *s_d=
        dynamic_cast<const dep_graph_domaint*>(&s);
      assert(s_d);
      const dependence_grapht::nodet &n=dep_graph[s_d->get_node_id()];

      // check for data dependency
      bool has_in_loop_dep=false;
      for(const auto &e : n.in)
        if(e.second.get()==dep_edget::DATA ||
           e.second.get()==dep_edget::BOTH)
        {
          has_in_loop_dep=true;
          break;
        }

      goto_programt::targett cur=target++;

      // move statement before loop if there is no dependency
      if(!has_in_loop_dep)
      {
        goto_programt::targett new_cur=
          goto_program.insert_before(loop.first);
        new_cur->swap(*cur);
        goto_program.instructions.erase(cur);
        transformed=true;
      }
    }
  }

  return transformed;
}

/*******************************************************************\

Function: transform_loops

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void transform_loops(
  goto_functionst &goto_functions,
  const namespacet &ns)
{
  // compute program dependence graph
  dependence_grapht dep_graph(ns);
  dep_graph(goto_functions, ns);

  bool needs_update=false;

  Forall_goto_functions(f_it, goto_functions)
  {
    while(transform_loops(f_it->second.body, dep_graph))
      needs_update=true;
  }

  if(needs_update)
  {
    // update counters etc.
    goto_functions.update();
  }
}
