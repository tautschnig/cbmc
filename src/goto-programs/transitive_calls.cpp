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

transitive_callst::transitive_callst(
  goto_functionst &_gf
, namespacet &_ns
) : goto_functions(_gf)
  , ns(_ns)
  , call_map()
{
  function_mapt fun_map = goto_functions.function_map;

  function_mapt::const_iterator it;
  for(it = fun_map.begin(); it != fun_map.end(); it++)
  {
    const symbolt &fun_symbol = ns.lookup(it->first);
    const std::string &fun_name = as_string(fun_symbol.name);

    // fun_name should be globally unique.
    assert(visited.find(fun_name) == visited.end());

    std::set<std::string> bucket =
      make_call_bucket_for(fun_name);

    /* We should always visit pthread_create. This is because each
     * pthread_create might spawn a different function, and we want to
     * include _all_ those functions. */
    if(fun_name.compare("c::pthread_create"))
    {
      visited.insert(fun_name);
      if(interested_in(fun_name))
        call_map.insert(std::make_pair(fun_name, bucket));
    }
    else
    /* fun_name is "c::pthread_create"; don't add fun_name to visted */
    {
      /* Each invocation of make_call_bucket_for("c::pthread_create")
       * returns a different set of functions transitively called. We
       * want to map to all of them, so union the sets. */
      std::set<std::string> &old_bucket = call_map[fun_name];
      std::set<std::string>::iterator buck_it;
      for(buck_it = bucket.begin(); buck_it != bucket.end(); buck_it++)
      {
        old_bucket.insert(*buck_it);
      }
    }
  }
}


std::set<std::string> transitive_callst::make_call_bucket_for(
  std::string fun_name
){
  if(visited.find(fun_name) != visited.end())
    return call_map[fun_name];

  std::set<std::string> worklist, accumulator;
  worklist.insert(fun_name);
  return make_call_bucket_for(worklist, accumulator, true);
}


std::set<std::string> transitive_callst::make_call_bucket_for(
  std::set<std::string> &worklist
, std::set<std::string> &accumulator
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

  /* pthread_create is a special case---we want it to be mapped to the
   * functions that are spawned by pthread_create, not called. */
  if(fun_name.compare("c::pthread_create") == 0)
  {
  }
  else
  {
    instructionst &instructions;
    bool found = false;
    function_mapt &fun_map = goto_functions.function_map;
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
    }

    // We might not have found the function because the function is not
    // defined in the program, for example it might be a libc function.
    if(!found)
      return make_call_bucket_for(worklist, accumulator, false);

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
  }
  return make_call_bucket_for(worklist, accumulator, false);
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

bool transitive_callst::interested_in(std::string fun_name)
{
  return true;

  return
    // Functions that are not part of the original program
    (   fun_name.compare("c::__actual_thread_spawn")
    &&  fun_name.compare("main")
    &&  fun_name.compare("c::__CPROVER_initialize")
    );
}
