/** @file   transitive_calls.cpp
 *  @author Kareem Khazem <karkhaz@karkhaz.com>
 *  @date   2014
 *
 *  If making changes to this file, please run the regression tests at
 *  regression/transitive_calls. The tests should all pass.
 *
 *  If you discover a bug, please add a regression that fails due to
 *  the bug.
 */
#include <util/dstring.h>
#include <util/json_utils.h>
#include <langapi/language_util.h>

#include "transitive_calls.h"

#include <iostream>
#include <algorithm>

transitive_callst::transitive_callst(
  goto_functionst &_gf
, namespacet &_ns
) : goto_functions(_gf)
  , ns(_ns)
  , call_map()
{
}


void transitive_callst::operator()()
{
  fun_list worklist;

  populate_initial(worklist);
  propagate_calls(worklist);
}


void transitive_callst::populate_initial(
   fun_list &worklist
){
  function_mapt &fun_map = goto_functions.function_map;

  function_mapt::const_iterator it;
  for(it = fun_map.begin(); it != fun_map.end(); it++)
  {
    const symbolt &fun_symbol = ns.lookup(it->first);
    const namet fun_name = as_string(fun_symbol.name);

    if(not_interested_in(fun_name))
      continue;

    fun_set bucket;

    if(it->second.body_available)
    {
      worklist.push_front(fun_name);

      const instructionst &instructions = it->second.body.instructions;
      instructionst::const_iterator ins;
      for(ins = instructions.begin(); ins != instructions.end(); ins++)
      {
        if(!ins->is_function_call())
          continue;

        namet child_name = function_of_instruction(*ins);
        if(not_interested_in(child_name))
          continue;

        bucket.insert(child_name);
      }
    }

    call_map.insert(std::make_pair(fun_name, bucket));
  }
}


transitive_callst::namet transitive_callst::function_of_instruction(
    const instructiont &instruction
){
  const exprt &function =
    to_code_function_call(instruction.code).function();

  const namet &call_name =
    as_string(to_symbol_expr(function).get_identifier());

  if(call_name.compare("c::pthread_create") != 0)
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


bool transitive_callst::not_interested_in(
    const namet &function
){
  return
     ( function.compare("c::__CPROVER_initialize")  ==  0
    || function.compare("c::__actual_thread_spawn")  ==  0
    || function.compare("main")  ==  0
    );
}


void transitive_callst::propagate_calls(
   fun_list &updated
){
  /* Algorithm:
   *  For each function F that has been updated
   *    For each function C called by F (i.e. in the bucket of F)
   *      If C has been updated
   *        Copy all functions called by C into the bucket of F
   *        If the bucket of F has changed
   *          F has been updated
   */
  while(!updated.empty())
  {
    namet work_item = updated.front();
    updated.pop_front();
    fun_set &work_bucket = call_map[work_item];
    
    fun_set::iterator call;
    for(call = work_bucket.begin(); call != work_bucket.end(); call++)
    {
      if(std::find(updated.begin(), updated.end(), *call)
          != updated.end())
      {
        fun_set &call_bucket = call_map[*call];

        bool needs_update = false;
        fun_set::iterator new_call;
        for(new_call  = call_bucket.begin();
            new_call != call_bucket.end();
            new_call++)
        {
          if(work_bucket.insert(*new_call).second)
            needs_update = true;
        }

        if(needs_update)
          updated.push_back(work_item);
      }
    }
  }
}


std::string transitive_callst::to_json()
{
  std::stringstream ret;
  json_utilt fmt(2);
  ret << "[" << fmt.ind();
  call_mapt::iterator it;
  for(it = call_map.begin(); it != call_map.end(); it++)
  {
    ret << "{" << fmt.ind();
    ret << "\"function_name\" : \"" << it->first << "\",";
    ret << fmt.nl() << "\"called_functions\" : [";
    ret << fmt.ind();

    std::set<std::string>::iterator call_it;
    for(call_it  = it->second.begin();
        call_it != it->second.end();
        call_it++)
    {
      ret << "\"" << *call_it << "\"";
      if(call_it != it->second.end() && (call_it != --it->second.end()))
        ret << "," << fmt.nl();
    }
  
    ret << fmt.und() << "]";
    ret << fmt.und() << "}";
    if(it != call_map.end() && (it != --call_map.end())){
      ret << "," << fmt.nl();
    }
  }
  ret << fmt.und() << "]";
  return ret.str();
}
