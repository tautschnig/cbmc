/*******************************************************************\

Module: Find cycles even when no entry function is known

Author: Michael Tautschnig, michael.tautschnig@cs.ox.ac.uk

\*******************************************************************/

#ifndef CPROVER_STATIC_CYCLES_H
#define CPROVER_STATIC_CYCLES_H

#include <goto-programs/goto_functions.h>
#include <goto-programs/goto_convert.h>
#include <goto-programs/remove_skip.h>
#include <goto-programs/remove_function_pointers.h>

#include <analyses/call_graph.h>
#include <analyses/goto_rw.h>
#include <analyses/may_alias.h>

#include <util/time_stopping.h>

#include "goto2graph.h"

class symbol_tablet;
class goto_functionst;

class static_cyclest{

private:
  typedef hash_set_cont<irep_idt, irep_id_hash> thread_functionst;
  typedef concurrent_cfg_baset<empty_cfg_nodet> cfgt;
  typedef goto_programt::instructiont::const_targett targett;
  typedef goto_programt::instructiont instructiont;

public:
  static_cyclest(symbol_tablet &_symbol_table,
      goto_functionst &_goto_functions,
      cfgt &cfg)
    : symbol_table(_symbol_table)
    , goto_functions(_goto_functions)
    , cfg(cfg)
    , b_output_event_source_locations(false)
    , r_output_event_source_locations(b_output_event_source_locations)
    , b_enable_egraph_dump(false)
    , r_enable_egraph_dump(b_enable_egraph_dump)
  {}

  /** @brief Starts the static cycles analysis */
  void operator()();

  /// @name Output options
  //{@
  
  /** If set to true, this static_cyclest will output the file and
   *  line numbers of all events, then returns without computing
   *  cycles.
   */
  bool &output_event_source_locations()
  { return r_output_event_source_locations; }

  /** If set to true, this static_cyclest will output a combined
   * egraph and cfg without computing static cycles.
   */
  bool &enable_egraph_dump()
  { return r_enable_egraph_dump; }

  //@}

private:
  void add_thread(
    const irep_idt &identifier,
    thread_functionst &thread_functions);

  void find_thread_start_thread(
    goto_programt::const_targett start_thread,
    const goto_programt &goto_program,
    thread_functionst &thread_functions);

  void get_function_symbols(
    const exprt &fn_ptr,
    const exprt &arg,
    std::set<symbol_exprt> &functions);

  void find_thread_function_call(
    const goto_programt::instructiont &function_call,
    thread_functionst &thread_functions);

  void find_threads(
    const goto_programt &goto_program,
    thread_functionst &thread_functions);

  void find_threads(
    thread_functionst &thread_functions);

  void filter_thread_functions(
    const call_grapht &call_map,
    thread_functionst &thread_functions);

  void collect_cycles_in_group(
    may_aliast &may_alias,
    const irep_idt &identifier);

  void form_thread_groups(
    thread_functionst &thread_functions,
    may_aliast &may_alias);

  typedef std::map<unsigned, instructiont> egraph2inst_mapt;
  egraph2inst_mapt egraph2inst;

  void build_egraph2inst(instrumentert &instrumenter);

  symbol_tablet   &symbol_table;
  goto_functionst &goto_functions;
  cfgt &cfg;

  bool  b_output_event_source_locations;
  bool &r_output_event_source_locations;
  bool  b_enable_egraph_dump;
  bool &r_enable_egraph_dump;
};

#endif

