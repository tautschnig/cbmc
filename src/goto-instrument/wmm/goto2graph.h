/*******************************************************************\

Module: Instrumenter

Author: Vincent Nimal

Date: 2012

\*******************************************************************/

#ifndef INSTRUMENTER_H
#define INSTRUMENTER_H

#include <map>
#include <sstream>

#include <util/graph.h>
#include <util/namespace.h>
#include <util/message.h>

#include <goto-programs/goto_program.h>
#include <goto-programs/cfg.h>

#include "event_graph.h"
#include "wmm.h"

class symbol_tablet;
class goto_functionst;
class local_may_aliast;
class may_aliast;

class instrumentert
{
public:
  /* reference to goto-functions and symbol_table */
  namespacet ns;
  goto_functionst &goto_functions; 

  /* alternative representation of graph (SCC) */
  std::map<unsigned,unsigned> map_vertex_gnode;
  graph<abstract_eventt> egraph_alt;

  /* data dependencies per thread */
  std::map<unsigned,data_dpt> map_dependencies;

  unsigned unique_id;

  /* rendering options */
  bool render_po_aligned;
  bool render_by_file;
  bool render_by_function;

  bool inline local(const irep_idt& id);

  void inline add_instr_to_interleaving (
    goto_programt::instructionst::iterator it,
    goto_programt& interleaving);

  /* deprecated */
  bool is_cfg_spurious(const event_grapht::critical_cyclet& cyc);

  unsigned cost(const event_grapht::critical_cyclet::delayt& e);

  typedef std::set<event_grapht::critical_cyclet> set_of_cyclest;
  void inline instrument_all_inserter(
    const set_of_cyclest& set);
  void inline instrument_one_event_per_cycle_inserter(
    const set_of_cyclest& set);
  void inline instrument_one_read_per_cycle_inserter(
    const set_of_cyclest& set);
  void inline instrument_one_write_per_cycle_inserter(
    const set_of_cyclest& set);
  void inline instrument_minimum_interference_inserter(
    const set_of_cyclest& set);
  void inline instrument_my_events_inserter(
    const set_of_cyclest& set, const std::set<unsigned>& events);

  void inline print_outputs_local(
    const std::set<event_grapht::critical_cyclet>& set,
    std::ofstream& dot,
    std::ofstream& ref,
    std::ofstream& output,
    std::ofstream& all,
    std::ofstream& table,
    memory_modelt model,
    bool hide_internals);

public:
  /* forward traversal of program */
  typedef std::list<unsigned> nodest;
  struct thread_eventst
  {
    nodest reads;
    nodest writes;
    nodest fences;

  };

  struct event_datat
  {
    std::map<unsigned, thread_eventst> events;
    std::set<goto_programt::const_targett> use_events_from;

    event_datat()
      :file("")
      ,line("")
      ,code("")
      ,function("")
    {}

    /** @brief  associates this event with a location in the source
     *          code.
     *
     * Used when we want to instrument the source code (using some
     * external tool, not cprover) to detect when events happen.
     *
     * @post  the get_*() functions return sensible values when called
     *        on this object.
     */
    void set_location(
        goto_programt::const_targett target
        , namespacet &ns
        , goto_functionst &goto_functions
        )
    {
      const source_locationt &loc = target->source_location;
      file = loc.get_file().c_str();
      line = loc.get_line().c_str();
      code = from_expr(ns, "", target->code);

      goto_functionst::function_mapt::iterator it;
      for(it  = goto_functions.function_map.begin();
          it != goto_functions.function_map.end();
          it++)
      {
        goto_programt &body = it->second.body;
        goto_programt::instructionst::iterator ins;
        for(ins  = body.instructions.begin();
            ins != body.instructions.end();
            ins++)
        {
          const locationt inner_loc = ins->location;
          if(inner_loc.get_file() == loc.get_file()
          && inner_loc.get_line() == loc.get_line()
          && ins->location_number == target->location_number)
            function = it->first.c_str();
        }
      }
    }

  public:
    
    /// @brief the file in which this event is located
    std::string get_file(){ return file; }

    /// @brief the line of the file where this event is located
    std::string get_line(){ return line; }

    /// @brief the function where this event is located
    std::string get_function(){ return function; }

    /// @brief pretty rep of the code
    std::string get_code()
    {
      if(code.substr(0, 4).compare("irep") == 0)
        return "<unknown_irep>";
      return code;
    }

    /// @brief is this a read?
    bool is_read()
    {
      std::map<unsigned, thread_eventst>::iterator ev;
      for(ev = events.begin(); ev != events.end(); ev++)
      {
        thread_eventst te = ev->second;
        if (!te.reads.empty())
            return true;
      }
      return false;
    }
    /// @brief is this a write?
    bool is_write()
    {
      std::map<unsigned, thread_eventst>::iterator ev;
      for(ev = events.begin(); ev != events.end(); ev++)
      {
        thread_eventst te = ev->second;
        if (!te.writes.empty())
            return true;
      }
      return false;
    }
    /// @brief is this a fence?
    bool is_fence()
    {
      std::map<unsigned, thread_eventst>::iterator ev;
      for(ev = events.begin(); ev != events.end(); ev++)
      {
        thread_eventst te = ev->second;
        if (!te.fences.empty())
            return true;
      }
      return false;
    }

  private:
    std::string file;
    std::string line;
    std::string code;
    std::string function;
  };

  /* per-thread control flow graph only, no inter-thread edges */
  typedef cfg_baset<event_datat> cfgt;
  cfgt cfg;

protected:
  /* message */
  messaget& message;

public:
  /** @brief Return a list of source locations of events in this
   * instrumentert.
   *
   * @pre set_location() should have been called for each event in the
   * cfg.
   */
  std::list<event_datat> event_source_locations()
  {
    std::list<event_datat> ret;
    for(unsigned i = 0; i < cfg.size(); i++)
    {
      event_datat ed = cfg[i];
      if(!ed.is_read()
      && !ed.is_write()
      && !ed.is_fence())
         continue;

      ret.push_front(ed);
    }
    return ret;
  }

  /* graph */
  event_grapht egraph;
protected:

  /* for thread marking (dynamic) */
  unsigned max_thread;
  /* current thread number */
  unsigned thread;

  /* relations between irep and Reads/Writes */
  typedef std::multimap<irep_idt,unsigned> id2nodet;
  typedef std::pair<irep_idt,unsigned> id2node_pairt;
  id2nodet map_reads, map_writes;

  /* dependencies */
  data_dpt data_dp;

  /* writes and reads to unknown addresses -- conservative */
  std::set<unsigned> unknown_read_nodes;
  std::set<unsigned> unknown_write_nodes;

  /* set of functions visited so far -- we don't handle recursive functions */
  std::set<irep_idt> functions_met;

  /* transformers */
  void extract_events_rw(
    may_aliast &may_alias,
    memory_modelt model,
    bool no_dependencies,
    goto_programt::const_targett target,
    thread_eventst &dest);
  void extract_events_fence(
    memory_modelt model,
    goto_programt::const_targett target,
    thread_eventst &dest);
  void extract_events(
    may_aliast &may_alias,
    memory_modelt model,
    bool no_dependencies,
    cfgt::entryt &cfg_entry);

  void add_po_edges(
    const nodest &from_events,
    const unsigned event_node,
    const unsigned event_gnode,
    const bool is_backward);
  void add_po_edges(
    const unsigned thread_nr,
    const cfgt::entryt &from,
    const cfgt::entryt &to,
    const unsigned event_node,
    const unsigned event_gnode);
  void add_po_edges(
    const cfgt::entryt &cfg_entry,
    const unsigned thread_nr,
    const unsigned event_node);
  void add_po_edges(
    const cfgt::entryt &cfg_entry,
    const unsigned thread_nr,
    const thread_eventst &thread_events);
  void add_edges_assign(
    const cfgt::entryt &cfg_entry,
    const thread_eventst &thread_events);
  void add_com_edges(
    const cfgt::entryt &cfg_entry,
    const thread_eventst &thread_events);
  void add_edges(
    const cfgt::entryt &cfg_entry,
    const unsigned thread_nr,
    const thread_eventst &thread_events);

public:

  /* graph split into strongly connected components */
  std::vector<std::set<unsigned> > egraph_SCCs;

  /* critical cycles */
  std::set<event_grapht::critical_cyclet> set_of_cycles;

  /* critical cycles per SCC */
  std::vector<std::set<event_grapht::critical_cyclet> > set_of_cycles_per_SCC;
  unsigned num_sccs;

  /* variables to instrument, locations of variables to instrument on 
     the cycles, and locations of all the variables on the critical cycles */
  /* TODO: those maps are here to interface easily with weak_mem.cpp, 
     but a rewriting of weak_mem can eliminate them */
  std::set<irep_idt> var_to_instr;
  std::multimap<irep_idt,source_locationt> id2loc;
  std::multimap<irep_idt,source_locationt> id2cycloc;

  instrumentert(symbol_tablet& _symbol_table, goto_functionst& _goto_f, 
    messaget& _message)
    :ns(_symbol_table), goto_functions(_goto_f), render_po_aligned(true), 
      render_by_file(false), render_by_function(false), message(_message),
      egraph(_message),
    max_thread(0),
    thread(0),
    write_counter(0),
    read_counter(0)
  {
  }

  /* abstracts goto-programs in abstract event graph, and computes
     the thread numbering and returns the max number */
  unsigned write_counter;
  unsigned read_counter;

  void forward_traverse_once(
    may_aliast &may_alias,
    memory_modelt model,
    bool no_dependencies,
    goto_programt::const_targett target);
  void forward_traverse_once(
    may_aliast &may_alias,
    memory_modelt model,
    bool no_dependencies);

  void propagate_events_in_po();

  void add_edges();

  unsigned build_event_graph(
    may_aliast &may_alias,
    memory_modelt model,
    bool no_dependencies,
    /* forces the duplication, with arrays or not; otherwise, arrays only */
    loop_strategyt duplicate_body);

  /* collects directly all the cycles in the graph */
  void collect_cycles(memory_modelt model)
  {
    egraph.collect_cycles(set_of_cycles,model);
    num_sccs = 0;
  }

  /* collects the cycles in the graph by SCCs */
  void collect_cycles_by_SCCs(memory_modelt model);

  /* filters cycles spurious by CFG */
  void cfg_cycles_filter();

  /* sets parameters for collection, if required */
  void set_parameters_collection(
    unsigned _max_var = 0,
    unsigned _max_po_trans = 0,
    bool _ignore_arrays = false)
  {
    egraph.set_parameters_collection(_max_var,_max_po_trans,_ignore_arrays);
  }

  /* builds the relations between unsafe pairs in the critical cycles and
     instructions to instrument in the code */

  /* strategies for instrumentation */
  void instrument_with_strategy(instrumentation_strategyt strategy);
  void instrument_my_events(const std::set<unsigned>& events);

  /* retrieves events to filter in the instrumentation choice
     with option --my-events */
  static std::set<unsigned> extract_my_events();

  /* sets rendering options */
  void set_rendering_options(bool aligned, bool file, bool function)
  {
    render_po_aligned = aligned;
    render_by_file = file;
    render_by_function = function;
    assert(!render_by_file || !render_by_function);
  }

  /* prints outputs:
     - cycles.dot: graph of the instrumented cycles  
     - ref.txt: names of the instrumented cycles
     - output.txt: names of the instructions in the code 
     - all.txt: all */
  void print_outputs(memory_modelt model, bool hide_internals);
};

#endif
