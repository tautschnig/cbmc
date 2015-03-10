/*******************************************************************\

Module: Symbolic Execution

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <cassert>
#include <iostream>

#include <util/std_expr.h>
#include <util/rename.h>
#include <util/symbol_table.h>
#include <util/replace_symbol.h>

#include "goto_symex.h"

/*******************************************************************\

Function: goto_symext::new_name

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_symext::new_name(symbolt &symbol)
{
  get_new_name(symbol, ns);
  new_symbol_table.add(symbol);
}

/*******************************************************************\

Function: goto_symext::vcc

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

int three_times=0;
void goto_symext::vcc(
  const exprt &vcc_expr,
  const std::string &msg,
  statet &state)
{
  total_vccs++;

  exprt expr=vcc_expr;

  // we are willing to re-write some quantified expressions
  rewrite_quantifiers(expr, state);

  // now rename, enables propagation    
  state.rename(expr, ns);
  
  // now try simplifier on it
  ++three_times;
  //if(three_times<4)
  //std::cerr << "simplify_vcc: " << from_expr(ns, "", expr) << std::endl;
  do_simplify(expr);
  //if(three_times<4)
  //std::cerr << "simplified_vcc: " << from_expr(ns, "", expr) << std::endl;

  if(expr.is_true()) return;
  
  state.guard.guard_expr(expr);

  // simplify guard expression
  do_simplify(expr);
  
  remaining_vccs++;
  target.assertion(state.guard.as_expr(), expr, msg, state.source);
}

/*******************************************************************\

Function: goto_symext::symex_assume

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_symext::symex_assume(statet &state, const exprt &cond)
{
  exprt simplified_cond=cond;

  do_simplify(simplified_cond);

  if(simplified_cond.is_true()) return;

  if(state.threads.size()==1)
  {
    exprt tmp=simplified_cond;
    state.guard.guard_expr(tmp);
    do_simplify(tmp);
    target.assumption(state.guard.as_expr(), tmp, state.source);
    state.guard.add(simplified_cond);
  }
  // symex_target_equationt::convert_assertions would fail to
  // consider assumptions of threads that have a thread-id above that
  // of the thread containing the assertion:
  // T0                     T1
  // x=0;                   assume(x==1);
  // assert(x!=42);         x=42;
  else
    state.guard.add(simplified_cond);

  if(state.atomic_section_id!=0 &&
     state.guard.is_false())
    symex_atomic_end(state);
}

/*******************************************************************\

Function: goto_symext::rewrite_quantifiers

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_symext::rewrite_quantifiers(exprt &expr, statet &state)
{
  if(expr.id()==ID_forall)
  {
    // forall X. P -> P
    // we keep the quantified variable unique by means of L2 renaming
    assert(expr.operands().size()==2);
    assert(expr.op0().id()==ID_symbol);
    symbol_exprt tmp0=
      to_symbol_expr(to_ssa_expr(expr.op0()).get_original_expr());
    symex_decl(state, tmp0);
    exprt tmp=expr.op1();
    expr.swap(tmp);
  }
}

/*******************************************************************\

Function: goto_symext::operator()

  Inputs:

 Outputs:

 Purpose: symex from given state

\*******************************************************************/

void goto_symext::operator()(
  statet &state,
  const goto_functionst &goto_functions,
  const goto_programt &goto_program)
{
  assert(!goto_program.instructions.empty());

  state.source=symex_targett::sourcet(goto_program);
  assert(!state.threads.empty());
  assert(!state.call_stack().empty());
  state.top().end_of_function=--goto_program.instructions.end();
  state.top().calling_location.pc=state.top().end_of_function;
  state.symex_target=&target;
  
  assert(state.top().end_of_function->is_end_function());

  const std::string cex_file=options.get_option("verify-cex");
  if(!cex_file.empty())
  {
    unsigned entry_node=0;
    if(read_graphml(cex_file, cex_graph, entry_node))
      throw "Failed to parse counterexample graph "+cex_file;

    state.cex_graph_nodes.insert(entry_node);

    typedef std::map< std::pair<irep_idt, irep_idt>,
                      std::set<goto_programt::const_targett> > loc_to_num_mapt;
    loc_to_num_mapt loc_to_num_map;

    forall_goto_functions(f_it, goto_functions)
    {
      if(!f_it->second.body_available) continue;

      forall_goto_program_instructions(i_it, f_it->second.body)
      {
        irep_idt line=i_it->source_location.get_line();
        if(line.empty()) continue;

        irep_idt file=i_it->source_location.get_file();
        if(file=="<built-in-additions>") continue;

        loc_to_num_map[std::make_pair(file, line)].insert(i_it);
      }
    }

    for(unsigned i=0; i<cex_graph.size(); ++i)
    {
      const graphmlt::nodet &node=cex_graph[i];

      for(graphmlt::edgest::const_iterator
          it=node.out.begin();
          it!=node.out.end();
          ++it)
      {
        const xmlt &xml_node=it->second.xml_node;
        std::string f, l;

        for(xmlt::elementst::const_iterator
            x_it=xml_node.elements.begin();
            x_it!=xml_node.elements.end();
            ++x_it)
        {
          if(x_it->get_attribute("key")=="originfile")
            f=x_it->data;
          else if(x_it->get_attribute("key")=="originline")
            l=x_it->data;
        }

        loc_to_num_mapt::const_iterator m_it=loc_to_num_map.end();
        if(!f.empty() && !l.empty())
          m_it=loc_to_num_map.find(std::make_pair(f, l));

        if(m_it!=loc_to_num_map.end())
          for(std::set<goto_programt::const_targett>::const_iterator
              t_it=m_it->second.begin();
              t_it!=m_it->second.end();
              ++t_it)
            target_to_cex_state[(*t_it)->location_number].insert(i);
      }
    }
  }

  while(!state.call_stack().empty())
  {
    symex_step(goto_functions, state);
    
    // is there another thread to execute?
    if(state.call_stack().empty() &&
       state.source.thread_nr+1<state.threads.size())
    {
      unsigned t=state.source.thread_nr+1;
      //std::cout << "********* Now executing thread " << t << std::endl;
      state.switch_to_thread(t);
    }
  }
}

/*******************************************************************\

Function: goto_symext::operator()

  Inputs:

 Outputs:

 Purpose: symex starting from given program

\*******************************************************************/

void goto_symext::operator()(
  const goto_functionst &goto_functions,
  const goto_programt &goto_program)
{
  statet state;
  operator() (state, goto_functions, goto_program);
}

/*******************************************************************\

Function: goto_symext::operator()

  Inputs:

 Outputs:

 Purpose: symex from entry point

\*******************************************************************/

void goto_symext::operator()(const goto_functionst &goto_functions)
{
  goto_functionst::function_mapt::const_iterator it=
    goto_functions.function_map.find(goto_functionst::entry_point());

  if(it==goto_functions.function_map.end())
    throw "the program has no entry point";

  const goto_programt &body=it->second.body;

  operator()(goto_functions, body);
}

/*******************************************************************\

Function: goto_symext::symex_step

  Inputs:

 Outputs:

 Purpose: do just one step

\*******************************************************************/

void goto_symext::symex_step(
  const goto_functionst &goto_functions,
  statet &state)
{
  #if 0
  std::cout << "\ninstruction type is " << state.source.pc->type << std::endl;
  std::cout << "Location: " << state.source.pc->source_location << std::endl;
  std::cout << "Guard: " << from_expr(ns, "", state.guard.as_expr()) << std::endl;
  std::cout << "Code: " << from_expr(ns, "", state.source.pc->code) << std::endl;
  #endif

  assert(!state.threads.empty());
  assert(!state.call_stack().empty());

  const goto_programt::instructiont &instruction=*state.source.pc;

  merge_gotos(state);

  // depth exceeded?
  {
    unsigned max_depth=options.get_unsigned_int_option("depth");
    if(max_depth!=0 && state.depth>max_depth)
      state.guard.add(false_exprt());
    state.depth++;
  }

  if(cex_graph.size()!=0 &&
     !state.guard.is_false())
  {
#if 0
    // simulate epsilon transitions
    std::list<unsigned> worklist;
    for(std::set<unsigned>::const_iterator
        it=state.cex_graph_nodes.begin();
        it!=state.cex_graph_nodes.end();
        ++it)
      worklist.push_back(*it);

    while(!worklist.empty())
    {
      const unsigned n=worklist.back();
      worklist.pop_back();

      const graphmlt::nodet &node=cex_graph[n];

      for(graphmlt::edgest::const_iterator
          it=node.out.begin();
          it!=node.out.end();
          ++it)
      {
        const xmlt &xml_node=it->second.xml_node;
        bool has_epsilon=true;
        for(xmlt::elementst::const_iterator
            x_it=xml_node.elements.begin();
            has_epsilon && x_it!=xml_node.elements.end();
            ++x_it)
        {
          if(x_it->get_attribute("key")=="tokens")
            has_epsilon=x_it->data.empty();
        }

        if(has_epsilon &&
           state.cex_graph_nodes.insert(it->first).second)
          worklist.push_back(it->first);
      }
    }
#endif

    std::map<unsigned, std::set<unsigned> >::const_iterator states_entry=
      target_to_cex_state.find(instruction.location_number);

    std::set<unsigned> before;
    if(state.cex_started ||
       states_entry!=target_to_cex_state.end())
    {
      before.swap(state.cex_graph_nodes);
#if 0
      std::cerr << "Swap yields " << before.size() << " candidates" << std::endl;
#endif
      state.cex_started=true;
    }
#if 0
    else
    {
      std::cerr << "No states mapping from " << instruction.source_location << std::endl;
      std::cerr << "Still have " << state.cex_graph_nodes.size() << " candidates" << std::endl;
      if(!state.cex_graph_nodes.empty())
        std::cerr << *state.cex_graph_nodes.begin() << std::endl;
      if(!state.cex_graph_nodes.empty())
        std::cerr << *(--state.cex_graph_nodes.end()) << std::endl;
    }
#endif

    // greedily make transitions feasible at the current instruction
    std::set<unsigned>::const_iterator c_it=states_entry->second.begin();
    for(std::set<unsigned>::const_iterator
        it=before.begin();
        it!=before.end();
        ++it)
    {
      const graphmlt::nodet &node=cex_graph[*it];
#if 0
      std::cerr << "Testing node " << node.node_name << std::endl;
#endif

      while(c_it!=states_entry->second.end() && *c_it<*it)
        ++c_it;
      if(c_it==states_entry->second.end() || *it<*c_it)
        ; // does not match the current instruction
      else
      {
        assert(*it==*c_it);

        // there could be more instructions matching this one
        if(!node.out.empty())
          state.cex_graph_nodes.insert(*it);

        for(graphmlt::edgest::const_iterator
            e_it=node.out.begin();
            e_it!=node.out.end();
            ++e_it)
        {
          if(!cex_graph[e_it->first].out.empty())
            state.cex_graph_nodes.insert(e_it->first);
#if 0
          std::cerr << "Transition " << node.node_name << " --> "
            << cex_graph[e_it->first].node_name << std::endl;
          std::cerr << e_it->second.xml_node;
#endif
        }

        ++c_it;
      }
    }

    if(state.cex_graph_nodes.empty())
      state.guard.add(false_exprt());
  }

  // actually do instruction
  switch(instruction.type)
  {
  case SKIP:
    if(!state.guard.is_false())
      target.location(state.guard.as_expr(), state.source);
    state.source.pc++;
    break;

  case END_FUNCTION:
    // do even if state.guard.is_false() to clear out frame created
    // in symex_start_thread
    symex_end_of_function(state);
    state.source.pc++;
    break;
  
  case LOCATION:
    if(!state.guard.is_false())
      target.location(state.guard.as_expr(), state.source);
    state.source.pc++;
    break;
  
  case GOTO:
    symex_goto(state);
    break;
    
  case ASSUME:
    if(!state.guard.is_false())
    {
      exprt tmp=instruction.guard;
      clean_expr(tmp, state, false);
      state.rename(tmp, ns);
      symex_assume(state, tmp);
    }

    state.source.pc++;
    break;

  case ASSERT:
    if(!state.guard.is_false())
    {
      std::string msg=id2string(state.source.pc->source_location.get_comment());
      if(msg=="") msg="assertion";
      exprt tmp(instruction.guard);
      clean_expr(tmp, state, false);
      vcc(tmp, msg, state);
    }

    state.source.pc++;
    break;
    
  case RETURN:
    if(!state.guard.is_false())
      return_assignment(state);

    state.source.pc++;
    break;

  case ASSIGN:
    if(!state.guard.is_false())
      symex_assign_rec(state, to_code_assign(instruction.code));

    state.source.pc++;
    break;

  case FUNCTION_CALL:
    if(!state.guard.is_false())
    {
      code_function_callt deref_code=
        to_code_function_call(instruction.code);

      if(deref_code.lhs().is_not_nil())
        clean_expr(deref_code.lhs(), state, true);

      clean_expr(deref_code.function(), state, false);

      Forall_expr(it, deref_code.arguments())
        clean_expr(*it, state, false);
    
      symex_function_call(goto_functions, state, deref_code);
    }
    else
      state.source.pc++;
    break;

  case OTHER:
    if(!state.guard.is_false())
      symex_other(goto_functions, state);

    state.source.pc++;
    break;

  case DECL:
    if(!state.guard.is_false())
      symex_decl(state);

    state.source.pc++;
    break;

  case DEAD:
    symex_dead(state);
    state.source.pc++;
    break;

  case START_THREAD:
    symex_start_thread(state);
    state.source.pc++;
    break;
  
  case END_THREAD:
    // behaves like assume(0);
    if(!state.guard.is_false())
      state.guard.add(false_exprt());
    state.source.pc++;
    break;
  
  case ATOMIC_BEGIN:
    symex_atomic_begin(state);
    state.source.pc++;
    break;
    
  case ATOMIC_END:
    symex_atomic_end(state);
    state.source.pc++;
    break;
    
  case CATCH:
    symex_catch(state);
    state.source.pc++;
    break;
  
  case THROW:
    symex_throw(state);
    state.source.pc++;
    break;
    
  case NO_INSTRUCTION_TYPE:
    throw "symex got NO_INSTRUCTION";
  
  default:
    throw "symex got unexpected instruction";
  }
}
