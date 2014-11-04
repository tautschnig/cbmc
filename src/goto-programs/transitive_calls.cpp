/** @file   transitive_calls.cpp
 *  @author Kareem Khazem <karkhaz@karkhaz.com>
 *  @date   2014
 */
#include <util/dstring.h>
#include <langapi/language_util.h>

#include "transitive_calls.h"

#include <iostream>

transitive_callst::transitive_callst(
  goto_functionst &_gf
, namespacet &_ns
) : goto_functions(_gf)
  , ns(_ns)
  , call_map()
  , indent_width(2)
  , indent_level(0)
{
  function_mapt fun_map = goto_functions.function_map;

  function_mapt::const_iterator it;
  for(it = fun_map.begin(); it != fun_map.end(); it++)
  {
    const symbolt &fun_symbol = ns.lookup(it->first);
    const std::string &fun_name = as_string(fun_symbol.name);

    // fun_name should be globally unique.
    assert(visited.find(fun_name) == visited.end());

    visited.insert(fun_name);
    std::set<std::string> bucket =
      make_call_bucket_for(fun_name);

    if(interested_in(fun_name))
      call_map.insert(std::make_pair(fun_name, bucket));
  }
}


std::set<std::string> transitive_callst::make_call_bucket_for(
  std::string fun_name
){
  std::set<std::string> worklist, accumulator;
  worklist.insert(fun_name);
  return make_call_bucket_for(worklist, accumulator, true);
}


std::set<std::string> transitive_callst::make_call_bucket_for(
  std::set<std::string> worklist
, std::set<std::string> accumulator
, bool first_call
){

  if(worklist.size() == 0)
    return accumulator;

  std::string fun_name = *(worklist.begin());
  worklist.erase(fun_name);
  assert(accumulator.find(fun_name) == accumulator.end());
  if(!first_call){
    accumulator.insert(fun_name);
  }

  instructionst instructions;
  bool found = false;
  function_mapt fun_map = goto_functions.function_map;
  function_mapt::const_iterator it;
  for(it = fun_map.begin(); it != fun_map.end(); it++)
  {
    if(it->second.body_available
    && fun_name.compare(as_string(ns.lookup(it->first).name)) == 0)
    {
      instructions = it->second.body.instructions;
      found = true;
      break;
    }
  }               // The C function we are looking for should be in
  assert(found);  // the goto_functions somewhere

  instructionst::iterator ins;
  for(ins = instructions.begin(); ins != instructions.end(); ins++)
  {
    if(!ins->is_function_call())
      continue;

    const exprt &function =
      to_code_function_call(ins->code).function();
    const std::string &call_name =
      as_string(to_symbol_expr(function).get_identifier());

    if(accumulator.find(call_name) == accumulator.end()
    && interested_in(call_name)
    ){
      worklist.insert(call_name);
    }
  }
  return make_call_bucket_for(worklist, accumulator, false);
}


std::string transitive_callst::to_json()
{
  std::stringstream ret;
  ret << "[" << ind();
  call_mapt::iterator it;
  for(it = call_map.begin(); it != call_map.end(); it++)
  {
    ret << "{" << ind();
    ret << "\"function_name\" : \"" << it->first << "\",";
    ret << nl() << "\"called_functions\" : [";
    ret << ind();

    std::set<std::string>::iterator call_it;
    for(call_it  = it->second.begin();
        call_it != it->second.end();
        call_it++)
    {
      ret << "\"" << *call_it << "\"";
      if(call_it != it->second.end() && (call_it != --it->second.end()))
        ret << "," << nl();
    }
  
    ret << und() << "]";
    ret << und() << "}";
    if(it != call_map.end() && (it != --call_map.end())){
      ret << "," << nl();
    }
  }
  ret << und() << "]";
  return ret.str();
}

bool transitive_callst::interested_in(std::string fun_name)
{
  return
    // Functions that are not part of the original program
    (   fun_name.compare("c::__actual_thread_spawn")
    &&  fun_name.compare("main")
    &&  fun_name.compare("c::__CPROVER_initialize")
    );
}
