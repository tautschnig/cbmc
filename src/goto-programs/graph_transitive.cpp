#include "graph_transitive.h"

void graph_transitivet::operator()()
{
  populate_initial();
  propagate_calls();
}

void graph_transitivet::populate_initial()
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

    foo.insert(std::make_pair(fun_name, node_number));
  }
}

void graph_transitivet::propagate_calls()
{
}

bool graph_transitivet::not_interested_in(namet fun_name)
{
  return
     ( fun_name.compare("__CPROVER_initialize")  ==  0
    || fun_name.compare("__actual_thread_spawn")  ==  0
    || fun_name.compare(id2string(goto_functions.entry_point()))  ==  0
    );
}


void graph_transitivet::output()
{

}
