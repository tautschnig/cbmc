/*******************************************************************\

Module: Find cycles even when no entry function is known

Author: Michael Tautschnig, michael.tautschnig@cs.ox.ac.uk

\*******************************************************************/

#include <iostream>
#include <algorithm>

#include <util/json_utils.h>

#include "static_cycles.h"

typedef hash_set_cont<irep_idt, irep_id_hash> thread_functionst;

/*******************************************************************\

Function: add_thread

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

void static_cyclest::add_thread(
  const irep_idt &identifier,
  thread_functionst &thread_functions)
{
  goto_functionst::function_mapt::const_iterator fn=
    goto_functions.function_map.find(identifier);

  if(fn!=goto_functions.function_map.end() &&
     !fn->second.body.instructions.empty())
    thread_functions.insert(identifier);
  else
    thread_functions.insert(ID_main);
}

/*******************************************************************\

Function: find_thread_start_thread

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

void static_cyclest::find_thread_start_thread(
  goto_programt::const_targett start_thread,
  const goto_programt &goto_program,
  thread_functionst &thread_functions)
{
  assert(start_thread->targets.size()==1);

  add_thread(start_thread->function, thread_functions);

  for(goto_programt::const_targett
      target=start_thread->targets.front();
      target!=goto_program.instructions.end() &&
      !target->is_end_thread() &&
      !target->is_end_function();
      ++target)
  {
    if(!target->is_function_call())
      continue;

    const code_function_callt &call=to_code_function_call(target->code);
    if(call.function().id()!=ID_symbol)
      continue;

    const irep_idt &f_name=
      to_symbol_expr(call.function()).get_identifier();
    add_thread(f_name, thread_functions);
  }
}

/*******************************************************************\

Function: get_function_symbols

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void static_cyclest::get_function_symbols(
  const exprt &fn_ptr,
  const exprt &arg,
  std::set<symbol_exprt> &functions)
{
  if(fn_ptr.id()==ID_typecast)
  {
    get_function_symbols(
      to_typecast_expr(fn_ptr).op(),
      arg,
      functions);
  }
  else if(fn_ptr.id()==ID_if)
  {
    const if_exprt &fn_ptr_if=to_if_expr(fn_ptr);

    get_function_symbols(fn_ptr_if.true_case(), arg, functions);
    get_function_symbols(fn_ptr_if.false_case(), arg, functions);
  }
  else if(fn_ptr.id()==ID_symbol ||
          fn_ptr.id()==ID_member ||
          fn_ptr.id()==ID_index)
  {
    assert(fn_ptr.type().id()==ID_pointer);

    code_function_callt c;
    c.function()=dereference_exprt(fn_ptr, fn_ptr.type().subtype());
    c.arguments().push_back(arg);

    goto_programt tmp;

    goto_programt::targett f=tmp.add_instruction(FUNCTION_CALL);
    f->code=c;

    tmp.add_instruction(END_FUNCTION);

    remove_function_pointers(symbol_table, goto_functions, tmp, false);

    forall_goto_program_instructions(it, tmp)
      if(it->is_function_call())
      {
        const code_function_callt &code=
          to_code_function_call(it->code);

        functions.insert(to_symbol_expr(code.function()));
      }
  }
  else
  {
    const address_of_exprt &address_of=to_address_of_expr(fn_ptr);

    functions.insert(to_symbol_expr(address_of.object()));
  }
}

/*******************************************************************\

Function: find_thread_function_call

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

void static_cyclest::find_thread_function_call(
  const goto_programt::instructiont &function_call,
  thread_functionst &thread_functions)
{
  const code_function_callt &call=
    to_code_function_call(function_call.code);

  if(call.function().id()!=ID_symbol)
    return;

  // pthread_create or kthread_create_on_node
  const irep_idt &f_name=
    to_symbol_expr(call.function()).get_identifier();
  const code_function_callt::argumentst &args=call.arguments();

  exprt thread_fn_ptr;
  exprt arg;

  if(f_name=="c::pthread_create")
  {
    assert(args.size()==4);
    thread_fn_ptr=args[2];
    arg=args[3];
  }
  else if(f_name=="c::kthread_create_on_node")
  {
    assert(args.size()>=4);
    thread_fn_ptr=args.front();
    arg=args[1];
  }
  else
    return;

  std::set<symbol_exprt> functions;
  get_function_symbols(thread_fn_ptr, arg, functions);

  for(std::set<symbol_exprt>::const_iterator it=functions.begin();
      it!=functions.end();
      ++it)
  {
    add_thread(it->get_identifier(), thread_functions);
  }

  add_thread(function_call.function, thread_functions);
}

/*******************************************************************\

Function: find_threads

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

void static_cyclest::find_threads(
  const goto_programt &goto_program,
  thread_functionst &thread_functions)
{
  forall_goto_program_instructions(it, goto_program)
    if(it->is_start_thread())
      find_thread_start_thread(it, goto_program, thread_functions);
    else if(it->is_function_call())
      find_thread_function_call(*it, thread_functions);
}

/*******************************************************************\

Function: find_threads

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

void static_cyclest::find_threads(
  thread_functionst &thread_functions)
{
  forall_goto_functions(f_it, goto_functions)
    if(f_it->first!="c::pthread_create" &&
       f_it->first!="c::kthread_create_on_node")
      find_threads(f_it->second.body, thread_functions);
}

/*******************************************************************\

Function: filter_thread_functions

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

void static_cyclest::filter_thread_functions(
  const call_grapht &call_map,
  thread_functionst &thread_functions)
{
  graph<visited_nodet<empty_edget> > call_graph;

  hash_map_cont<irep_idt, unsigned, irep_id_hash> fn_to_node;
  for(thread_functionst::const_iterator it=thread_functions.begin();
      it!=thread_functions.end();
      ++it)
  {
    unsigned n=call_graph.add_node();
    fn_to_node.insert(std::make_pair(*it, n));
  }

  for(std::multimap<irep_idt, irep_idt>::const_iterator
      it=call_map.graph.begin();
      it!=call_map.graph.end();
      ++it)
  {
    if(fn_to_node.find(it->first)==fn_to_node.end())
      fn_to_node.insert(std::make_pair(it->first, call_graph.add_node()));
    if(fn_to_node.find(it->second)==fn_to_node.end())
      fn_to_node.insert(std::make_pair(it->second, call_graph.add_node()));

    call_graph.add_edge(
      fn_to_node[it->first],
      fn_to_node[it->second]);
  }

  thread_functionst before;
  before.swap(thread_functions);

  for(thread_functionst::const_iterator it=before.begin();
      it!=before.end();
      ++it)
  {
    const irep_idt &identifier=*it;
    const unsigned n=fn_to_node[identifier];

    if(call_graph[n].visited)
      continue;

    if(call_graph.in(n).empty())
    {
      add_thread(identifier, thread_functions);
      call_graph.visit_reachable(n);
      continue;
    }

    graph<visited_nodet<empty_edget> >::patht path;
    call_graph.shortest_loop(n, path);
    if(!path.empty())
    {
      add_thread(identifier, thread_functions);
      call_graph.visit_reachable(n);
    }
  }

  std::cerr << "Thread entry candidates:" << std::endl;
  for(thread_functionst::const_iterator it=thread_functions.begin();
      it!=thread_functions.end();
      ++it)
    std::cerr << *it << std::endl;
}

/*******************************************************************\

Function: collect_cycles_in_group

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

void static_cyclest::collect_cycles_in_group(
  may_aliast &may_alias,
  const irep_idt &identifier)
{
  const namespacet ns(symbol_table);
  null_message_handlert mh;
  messaget message(mh);
  instrumentert instrumenter(symbol_table, goto_functions, message);

  absolute_timet graph_time_start=current_time();
  {
    instrumenter.build_event_graph(may_alias, Static_Weak, false, no_loop);
    instrumenter.cfg(goto_functions);

    goto_functionst::function_mapt::const_iterator fn=
      goto_functions.function_map.find(identifier);
    assert(fn!=goto_functions.function_map.end() &&
           !fn->second.body.instructions.empty());
    instrumenter.forward_traverse_once(
      may_alias,
      Static_Weak,
      false,
      fn->second.body.instructions.begin());

    instrumenter.propagate_events_in_po();

    if(output_event_source_locations())
    {
      std::list<instrumentert::event_datat> events =
        instrumenter.event_source_locations();
      json_utilt j(2);
      std::stringstream ss;
      ss << "[" << j.ind();
      std::list<instrumentert::event_datat>::iterator it;
      std::list<instrumentert::event_datat>::iterator last
        = events.end();
      --last;
      for(it = events.begin(); it != events.end(); it++)
      {
        ss << "{" << j.ind();

        ss << "\"file\" : \""
           << it->get_file()
           << "\"";

        ss << "," << j.nl()
           << "\"line\" : \""
           << it->get_line()
           << "\"";
        
        ss << "," << j.nl()
           << "\"code\" : \""
           << it->get_code()
           << "\"";
        
        ss << "," << j.nl()
           << "\"function\" : \""
           << it->get_function()
           << "\"";
        
        ss << "," << j.nl()
           << "\"read?\"  : "
           << (it->is_read() ? "true" : "false");
        
        ss << "," << j.nl()
           << "\"write?\" : "
           << (it->is_write() ? "true" : "false");
        
        ss << "," << j.nl()
           << "\"fence?\" : "
           << (it->is_fence() ? "true" : "false");

        ss << j.und() << "}";

        if(last != it)
          ss << "," << j.nl();
      }
      ss << j.und() << "]" << j.nl();
      std::cout << ss.str();
      return;
    }

    instrumenter.add_edges();
  }
  std::cerr << "Time event graph construction: "
            << current_time()-graph_time_start << std::endl;

  std::cout << "Number of reads: " <<  instrumenter.read_counter <<std::endl;
  std::cout << "Number of writes: " << instrumenter.write_counter <<std::endl;

  instrumenter.egraph.filter_thin_air=false;
  instrumenter.egraph.filter_uniproc=false;

  // We don't need expensive cycle search if we're just dumping dot
  if(enable_egraph_dump())
  {
    instrumenter.output_dot(std::cout);
    return;
  }

  absolute_timet cycle_enum_time_start=current_time();
  instrumenter.collect_cycles(Static_Weak);
  std::cout << "Time cycle enumeration: "
            << current_time()-cycle_enum_time_start << std::endl;

  for(std::set<event_grapht::critical_cyclet>::const_iterator
      it=instrumenter.set_of_cycles.begin();
      it!=instrumenter.set_of_cycles.end();
      ++it)
  {
    std::cout << "Source: " << it->print_output() << std::endl;
    std::cout << "Cycle:" << it->print_name(Static_Weak, false) << std::endl;
  }
}

/*******************************************************************\

Function: form_thread_groups

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

void static_cyclest::form_thread_groups(
  thread_functionst &thread_functions,
  may_aliast &may_alias)
{
  forall_goto_functions(it, goto_functions)
    add_thread(it->first, thread_functions);

  call_grapht call_map(goto_functions);

  filter_thread_functions(call_map, thread_functions);

  const namespacet ns(symbol_table);

  typedef std::list<std::pair<std::set<irep_idt>, thread_functionst> >
          thread_groupst;
  thread_groupst thread_groups;

  for(thread_functionst::const_iterator it=thread_functions.begin();
      it!=thread_functions.end();
      ++it)
  {
    const irep_idt &root_fn=*it;

    hash_set_cont<irep_idt, irep_id_hash> called_funcs;
    called_funcs.insert(root_fn);

    std::list<irep_idt> wl;
    wl.push_back(root_fn);

    while(!wl.empty())
    {
      irep_idt caller=wl.back();
      wl.pop_back();

      std::pair<std::multimap<irep_idt, irep_idt>::const_iterator,
        std::multimap<irep_idt, irep_idt>::const_iterator>
          eq_iterators=call_map.graph.equal_range(caller);
      for( ;
           eq_iterators.first!=eq_iterators.second;
           ++(eq_iterators.first))
        if(called_funcs.insert(eq_iterators.first->second).second)
          wl.push_back(eq_iterators.first->second);
    }

    thread_groups.push_back(std::make_pair(
        std::set<irep_idt>(),
        thread_functionst()));
    std::set<irep_idt> &rw_symbols=thread_groups.back().first;
    thread_groups.back().second.insert(root_fn);

    for(hash_set_cont<irep_idt, irep_id_hash>::const_iterator
        c_it=called_funcs.begin();
        c_it!=called_funcs.end();
        ++c_it)
    {
      goto_functionst::function_mapt::const_iterator fn=
        goto_functions.function_map.find(*c_it);

      if(fn==goto_functions.function_map.end() ||
         fn->second.body.instructions.empty())
        continue;

      const goto_programt &goto_program=fn->second.body;

      rw_range_set_dereft rw_set(ns, may_alias);
      goto_rw(goto_program, rw_set);

      forall_rw_range_set_w_objects(w_it, rw_set)
      {
        const irep_idt &identifier=w_it->first;
        if(!symbol_table.has_symbol(identifier) ||
           !ns.lookup(identifier).is_shared())
          continue;

        rw_symbols.insert(identifier);
      }

      forall_rw_range_set_r_objects(r_it, rw_set)
      {
        const irep_idt &identifier=r_it->first;
        if(!symbol_table.has_symbol(identifier) ||
           !ns.lookup(identifier).is_shared())
          continue;

        rw_symbols.insert(identifier);
      }
    }
  }

  for(thread_groupst::iterator it1=thread_groups.begin();
      it1!=thread_groups.end();
      ) // no ++it1
  {
    bool merged=it1->first.empty();
    for(thread_groupst::iterator next=it1, it2=++next;
        it2!=thread_groups.end() && !merged;
        ++it2)
    {
      std::list<irep_idt> intersection;
      std::set_intersection(
        it1->first.begin(), it1->first.end(),
        it2->first.begin(), it2->first.end(),
        std::back_inserter(intersection));

      if(intersection.empty())
        continue;

      std::copy(it1->first.begin(), it1->first.end(),
                std::inserter(it2->first, it2->first.begin()));
      it2->second.insert(it1->second.begin(), it1->second.end());
      merged=true;
    }

    if(merged ||
       (it1->second.size()==1 && it1->first.empty()))
      thread_groups.erase(it1++);
    else
      ++it1;
  }

  if(thread_groups.empty())
  {
    std::cout << "No thread groups" << std::endl;
    return;
  }

  assert(goto_functions.function_map.find("$$thread_dummy")==
         goto_functions.function_map.end());
  goto_functionst::goto_functiont &thread_dummy=
    goto_functions.function_map["$$thread_dummy"];
  thread_dummy.type=code_typet();
  thread_dummy.body_available=true;
  goto_programt &body=thread_dummy.body;

  for(thread_groupst::const_iterator it=thread_groups.begin();
      it!=thread_groups.end();
      ++it)
  {
    std::cout << "Thread group:" << std::endl;

    body.clear();

    null_message_handlert mh;

    code_function_callt f;
    f.lhs().make_nil();

    for(thread_functionst::const_iterator
        t_it=it->second.begin();
        t_it!=it->second.end();
        ++t_it)
    {
      const irep_idt identifier=*t_it;
      std::cout << identifier << std::endl;

      f.function()=symbol_exprt(identifier, code_typet());
      const code_labelt label("__CPROVER_ASYNC_0", f);

      // add 3 threads executing this function
      for(int i=0; i<3; ++i)
        goto_convert(label, symbol_table, body, mh);
    }

    body.add_instruction(END_FUNCTION);
    body.compute_target_numbers();
    remove_skip(body);
    body.update();

    collect_cycles_in_group(
      may_alias,
      "$$thread_dummy");
  }
}

/*******************************************************************\

Function: static_cycles

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

void static_cyclest::operator()()
{
  absolute_timet total_time_start=current_time();

  hash_set_cont<irep_idt, irep_id_hash> thread_functions;
  find_threads(thread_functions);

  bool is_linked=
    goto_functions.function_map.find(ID_main)
    !=goto_functions.function_map.end();

  if(is_linked &&
     thread_functions.empty())
  {
    std::cout << "No thread spawn found in linked binary" << std::endl;
    return;
  }

  const namespacet ns(symbol_table);
  absolute_timet alias_time_start=current_time();
  may_aliast may_alias(goto_functions, ns);
  std::cerr << "Time alias analysis: "
            << current_time()-alias_time_start << std::endl;

  if(!is_linked)
    form_thread_groups(thread_functions, may_alias);
  else
    collect_cycles_in_group(may_alias, ID_main);

  std::cerr << "Time static cycle search: "
            << current_time()-total_time_start << std::endl;
}
