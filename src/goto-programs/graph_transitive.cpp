/** @file   transitive_calls.cpp
 *  @author Kareem Khazem <karkhaz@karkhaz.com>
 *  @date   2015
 *
 *  If making changes to this file, please run the regression tests at
 *  regression/transitive_calls. The tests should all pass.
 *
 *  If you discover a bug, please add a regression that fails due to
 *  the bug.
 */
#include "graph_transitive.h"

#include <util/dot.h>
#include <util/json_utils.h>

#include <iostream>
#include <sstream>

void graph_transitivet::operator()()
{
  add_functions();
  add_calls();
}


void graph_transitivet::add_functions()
{
  function_mapt &fun_map = goto_functions.function_map;

  function_mapt::const_iterator it;
  for(it = fun_map.begin(); it != fun_map.end(); it++)
  {
    const symbolt &fun_symbol = ns.lookup(it->first);
    const namet fun_name = as_string(fun_symbol.name);

    if(not_interested_in(fun_name))
      continue;

    unsigned node_number = g.add_node();
    g[node_number].name() = fun_name;

    fun2graph.insert(std::make_pair(fun_name, node_number));
  }
}


void graph_transitivet::add_calls()
{
  function_mapt &fun_map = goto_functions.function_map;

  function_mapt::const_iterator it;
  for(it = fun_map.begin(); it != fun_map.end(); it++)
  {
    const symbolt &caller_symbol = ns.lookup(it->first);
    const namet caller_name = as_string(caller_symbol.name);

    if(not_interested_in(caller_name))
      continue;

    if(it->second.body_available())
    {
      unsigned caller_number = fun2graph[caller_name];

      const instructionst &instructions = it->second.body.instructions;
      instructionst::const_iterator ins;
      for(ins = instructions.begin(); ins != instructions.end(); ins++)
      {
        if(!ins->is_function_call())
          continue;

        namet child_name = name_of_call(*ins);
        if(not_interested_in(child_name)
        || (child_name.compare("") == 0)
        ) continue;

        unsigned child_number = fun2graph[child_name];

        g.add_edge(caller_number, child_number);
      }
    }
  }
}


graph_transitivet::namet graph_transitivet::name_of_call(
    const instructiont &instruction)
{

  const exprt &function =
    to_code_function_call(instruction.code).function();

  if(function.id() != ID_symbol)
    return "";

  const namet &call_name =
    as_string(to_symbol_expr(function).get_identifier());

  if(call_name.compare("pthread_create") != 0)
  {
    return call_name;
  }
  else /* This instruction is a pthread_create spawn */
  {
    const argumentst &args =
      to_code_function_call(instruction.code).arguments();

    exprt fun_ptr = args[2];

    if(fun_ptr.id() != ID_address_of)
      throw "Unrecognised third parameter to pthread_create";

    namet fun_ptr_name =
      fun_ptr           // exprt
      .get_sub()[0]     // symbol
      .get_named_sub()["identifier"]
      .pretty();        // something like c::foo

    return fun_ptr_name;
  }
}


bool graph_transitivet::not_interested_in(namet fun_name)
{
  return
     ( fun_name.compare("__CPROVER_initialize")  ==  0
    || fun_name.compare("__actual_thread_spawn")  ==  0
    || fun_name.compare(id2string(goto_functions.entry_point()))  ==  0
    );
}


void graph_transitivet::output_dot(std::ostream &out)
{
  digraph_factoryt<int> fact;

  for(unsigned i = 0; i < g.size(); i++)
  {
    fact.node(i).set("label", g[i].name());
    for(unsigned j = 0; j < g.size(); j++)
    {
      if(g.has_edge(i, j))
      {
        fact.edge(i, j);
      }
    }
  }

  fact.output(out);
}


void graph_transitivet::output_json(std::ostream &out)
{
  std::stringstream ss;
  json_utilt f(2);
  
  ss << "[" << f.ind();

  unsigned g_size = g.size();

  for(unsigned i = 0; i < g_size; i++)
  {
    ss << "{" << f.ind();
    ss << "\"function_name\" : \"" << g[i].name() << "\",";
    ss << f.nl() << "\"called_functions\" : [" << f.ind();

    namest called_funs;
    get_transitive_calls(g[i].name(), called_funs);

    namest::iterator call;
    for(call = called_funs.begin(); call != called_funs.end(); call++)
    {
      ss << "\"" << *call << "\"";

      if(call != called_funs.end() && (call != --called_funs.end()))
        ss << "," << f.nl();
    }

    ss << f.und() << "]";

    ss << f.und() << "}";

    if(i < g_size - 1)
      ss << "," << f.nl();
  }
  
  ss << f.und() << "]";
  ss << f.nl();

  out << ss.str();
}


void graph_transitivet::get_transitive_calls(
  namet &function,
  namest &calls
){

  clear_visited();

  assert(fun2graph.find(function) != fun2graph.end());

  unsigned function_node = fun2graph[function];
  std::list<unsigned> worklist;
  worklist.push_back(function_node);

  /* Usually, `function' will only be on the worklist once. If we see
   * it on the worklist a second time, then it must have transitively
   * called itself. Only add `function' to `calls' under that
   * circumstance.
   */
  bool function_seen_once = false;

  while(!worklist.empty())
  {
    unsigned work_item = worklist.front();
    worklist.pop_front();

    if(work_item != function_node
    || function_seen_once)
      g[work_item].visited = true;
    else
      function_seen_once = true;

    for(unsigned i = 0; i < g.size(); i++)
    {
      if(g[i].visited)
        continue;

      if(g.has_edge(work_item, i))
      {
        worklist.push_back(i);
        calls.push_back(g[i].name());
      }

    }
  }
}


void graph_transitivet::clear_visited()
{
  for(unsigned i = 0; i < g.size(); i++)
    g[i].visited = false;
}
