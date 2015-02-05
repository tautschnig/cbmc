#include <util/dstring.h>
#include <util/json_utils.h>
#include <langapi/language_util.h>

#include "spawned_funs.h"

#include <iostream>
#include <sstream>

spawned_funst::spawned_funst(
  goto_functionst &_gf
, namespacet &_ns
) : goto_functions(_gf)
  , ns(_ns)
  , spawned_functions()
{
  function_mapt &fun_map = goto_functions.function_map;

  spawned_functions.insert("c::main");

  function_mapt::const_iterator it;
  for(it = fun_map.begin(); it != fun_map.end(); it++)
  {
    if(!it->second.body_available())
      continue;

    instructionst instructions = it->second.body.instructions;
    instructionst::iterator ins;
    for(ins = instructions.begin(); ins != instructions.end(); ins++)
    {
      if(!ins->is_function_call())
        continue;

      const exprt &function =
        to_code_function_call(ins->code).function();
      const std::string &call_name =
        as_string(to_symbol_expr(function).get_identifier());

      if(call_name.compare("c::pthread_create"))
        continue;

      spawned_functions.insert(
        function_pointer_of_pthread_create(*ins));
    }
  }
}

std::string spawned_funst::function_pointer_of_pthread_create(
  instructiont &pthread_create
){
  if(!pthread_create.is_function_call())
    throw "Instruction doesn't look like a pthread_create";

  const exprt &function =
    to_code_function_call(pthread_create.code).function();
  const std::string &call_name =
    as_string(to_symbol_expr(function).get_identifier());

  if(call_name.compare("c::pthread_create"))
    throw "Instruction doesn't look like a pthread_create";

  const argumentst &args =
    to_code_function_call(pthread_create.code).arguments();

  exprt fun_ptr = args[2];
  
  /* I only deal with the case that the third argument of
   * pthread_create is a straight function pointer. If we fail a
   * lot here, then we need to investigate what other edge cases
   * exist and then check for them.
   * There's no way of doing the following code cleanly, as the
   * third arg to pthread_create could have any form, so we need
   * to mess around with the irept directly.
   */
  if(fun_ptr.id() != ID_address_of)
    assert(false);

  std::string fun_ptr_name =
    fun_ptr             // exprt
    .get_sub()[0]       // symbol
    .get_named_sub()["identifier"]
    .pretty();          // something like c::foo

  return fun_ptr_name;
}

std::string spawned_funst::to_json()
{
  std::stringstream ret;
  json_utilt fmt(2);
  ret << "[" << fmt.ind();

  std::set<std::string>::iterator it;
  for(it  = spawned_functions.begin();
      it != spawned_functions.end();
      it++)
  {
    ret << "\"" << *it << "\"";

    if(it != spawned_functions.end()
    && it != --spawned_functions.end())
      ret << "," << fmt.nl();
  }
  
  ret << fmt.und() << "]" << fmt.nl();

  return ret.str();
}
