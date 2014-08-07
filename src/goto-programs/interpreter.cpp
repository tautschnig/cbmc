/*******************************************************************\

Module: Interpreter for GOTO Programs

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

/// \file
/// Interpreter for GOTO Programs

#include "interpreter.h"

#include <cctype>
#include <cstdio>
#include <iostream>
#include <algorithm>
#include <fstream>

#include <util/std_types.h>
#include <util/symbol_table.h>

#include "interpreter.h"
#include "interpreter_class.h"

void interpretert::operator()()
{
  done=false;
  silent = false;

  while (!done)
  {
    run_upto_main = false;
    main_called = false;
    restart = false;
    running = false;
    batch_mode = false;
    commands.clear();

  build_memory_map();

    fix_argc(); //fix issue with main(int argc, char **argv)

  const goto_functionst::function_mapt::const_iterator
    main_it=goto_functions.function_map.find(goto_functionst::entry_point());

  if(main_it==goto_functions.function_map.end())
    throw "main not found";

    const goto_functionst::goto_functiont &goto_function=main_it->second;

    if(!goto_function.body_available)
      throw "main has no body";

    PC=goto_function.body.instructions.begin();
    function=main_it;
    initial_function = function;

    show_function_start_msg();

    next_stop_PC = (initial_function->second).body.instructions.end();
    next_stop_PC_set = false;
    step_out = false;
    completed = false;

    while(!done && !restart)
    {
      show_state();
      try
{
        command();
        if(!done && !restart && !completed) step();
      }
      catch(const std::string &e)
  {
        std::cout << "EXCEPTION ENCOUNTERED:" << e << std::endl;
      }
    }
  }
}

/*******************************************************************\

Function: interpretert::show_state

Inputs:

Outputs:

Purpose:

\*******************************************************************/

void interpretert::show_state() const
{
  show_state(false);
}

/*******************************************************************\

Function: interpretert::show_state

Inputs:

Outputs:

Purpose:

\*******************************************************************/

void interpretert::show_state(bool force) const
  {
  if (!force && silent) return;

  if (completed)
    {
    std::cout << std::endl;
    std::cout << std::endl;
    std::cout << "=============== That is the end ===================="
      << std::endl;
      return;
    }

  std::cout << std::endl;
  std::cout << "----------------------------------------------------"
    << std::endl;

  if(PC==function->second.body.instructions.end())
    {
    std::cout << "End of function '"
      << function->first << "'" << std::endl;
    }
  else
    function->second.body.output_instruction(ns, function->first, std::cout, PC);

  std::cout << std::endl;

  show_watches();
}

void interpretert::step()
{
  if (step_out && !next_stop_PC_set && !call_stack.empty())
  {
    stack_framet &frame = call_stack.top();
    next_stop_PC = frame.return_PC;
    next_stop_PC_set = true;
  }

  if(PC==function->second.body.instructions.end())
  {
    if(call_stack.empty())
    {
      completed = true;
      batch_mode = false;
      running = false;
      reading_from_queue = false;
      while (!queued_commands.empty())
      {
        queued_commands.pop();
      }
    }
    else
    {
      PC=call_stack.top().return_PC;
      function=call_stack.top().return_function;
      stack_pointer=call_stack.top().old_stack_pointer;
      call_stack.pop();
    }

    reset_next_PC();

    return;
  }

  next_PC=PC;
  next_PC++;

  switch(PC->type)
  {
  case GOTO:
    execute_goto();
    break;

  case ASSUME:
    execute_assume();
    break;

  case ASSERT:
    execute_assert();
    break;

  case OTHER:
    execute_other();
    break;

  case DECL:
    execute_decl();
    break;

  case SKIP:
  case LOCATION:
  case END_FUNCTION:
    break;

  case RETURN:
    if(call_stack.empty())
      throw "RETURN without call"; // NOLINT(readability/throw)

    if(PC->code.operands().size()==1 &&
       call_stack.top().return_value_address!=0)
    {
      std::vector<mp_integer> rhs;
      evaluate(PC->code.op0(), rhs);
      assign(call_stack.top().return_value_address, rhs);
    }

    next_PC=function->second.body.instructions.end();
    break;

  case ASSIGN:
    execute_assign();
    break;

  case FUNCTION_CALL:
    execute_function_call();
    break;

  case START_THREAD:
    throw "START_THREAD not yet implemented"; // NOLINT(readability/throw)

  case END_THREAD:
    throw "END_THREAD not yet implemented"; // NOLINT(readability/throw)
    break;

  case ATOMIC_BEGIN:
    throw "ATOMIC_BEGIN not yet implemented"; // NOLINT(readability/throw)

  case ATOMIC_END:
    throw "ATOMIC_END not yet implemented"; // NOLINT(readability/throw)

  case DEAD:
    throw "DEAD not yet implemented"; // NOLINT(readability/throw)

  default:
    throw "encountered instruction with undefined instruction type";
  }

  PC=next_PC;

  reset_next_PC();
}

void interpretert::execute_goto()
{
  if(evaluate_boolean(PC->guard))
  {
    if(PC->targets.empty())
      throw "taken goto without target";

    if(PC->targets.size()>=2)
      throw "non-deterministic goto encountered";

    next_PC=PC->targets.front();
  }
}

void interpretert::execute_other()
{
  const irep_idt &statement=PC->code.get_statement();

  if(statement==ID_expression)
  {
    assert(PC->code.operands().size()==1);
    std::vector<mp_integer> rhs;
    evaluate(PC->code.op0(), rhs);
  }
  else if(statement == ID_printf)  // siqing
  {                                //   supports
    execute_printf();              //     printf()
  }                                //       now
  else
    throw "unexpected OTHER statement: "+id2string(statement);
}

void interpretert::execute_decl()
{
  assert(PC->code.get_statement()==ID_decl);
}

void interpretert::execute_assign()
{
  const code_assignt &code_assign=
    to_code_assign(PC->code);

  std::vector<mp_integer> rhs;
  evaluate(code_assign.rhs(), rhs);

  if(!rhs.empty())
  {
    mp_integer address=evaluate_address(code_assign.lhs());
    unsigned size=get_size(code_assign.lhs().type());

    if(size!=rhs.size())
      std::cout << "!! failed to obtain rhs ("
                << rhs.size() << " vs. "
                << size << ")\n";
    else
      assign(address, rhs);
  }
}

void interpretert::assign(
  mp_integer address,
  const std::vector<mp_integer> &rhs)
{
  for(unsigned i=0; i<rhs.size(); i++, ++address)
  {
    if(address<memory.size())
    {
      memory_cellt &cell=memory[integer2unsigned(address)];
      if (!silent)
      {
        std::cout << "** assigning " << cell.identifier
                  << "[" << cell.offset << "]:=" << rhs[i] << '\n';
      }
      cell.value=rhs[i];
    }
  }
}

void interpretert::execute_assume()
{
  if(!evaluate_boolean(PC->guard))
    throw "assumption failed";
}

void interpretert::execute_assert()
{
  if(!evaluate_boolean(PC->guard))
    throw "assertion failed";
}

void interpretert::execute_function_call()
{
  const code_function_callt &function_call=
    to_code_function_call(PC->code);

  // function to be called
  mp_integer a=evaluate_address(function_call.function());

  if(a==0)
    throw "function call to NULL";
  else if(a>=memory.size())
    throw "out-of-range function call";

  const memory_cellt &cell=memory[integer2size_t(a)];
  const irep_idt &identifier=cell.identifier;
  bool calling_main = id2string(identifier) == "c::main";
  bool replacing_main = false;

  goto_functionst::function_mapt::const_iterator f_it = 
    goto_functions.function_map.end();

  if (entry_function != "" && calling_main)
  {
    f_it = find_function(entry_function);
    if (f_it == goto_functions.function_map.end())
    {
      f_it = goto_functions.function_map.find(identifier);
    }
    else
    {
      replacing_main = true;
    }
  }
  else
  {
    f_it = goto_functions.function_map.find(identifier);
  }

  if(f_it==goto_functions.function_map.end())
    throw "failed to find function "+id2string(identifier);

  if (!main_called && calling_main)
	  main_called = true;

  // return value
  mp_integer return_value_address;

  if(function_call.lhs().is_not_nil())
    return_value_address=
      evaluate_address(function_call.lhs());
  else
    return_value_address=0;

  // values of the arguments
  std::vector<std::vector<mp_integer> > argument_values;
  if (!replacing_main)
  {
  argument_values.resize(function_call.arguments().size());

  for(std::size_t i=0; i<function_call.arguments().size(); i++)
    evaluate(function_call.arguments()[i], argument_values[i]);
  }

  // do the call

  if(f_it->second.body_available())
  {
    call_stack.push(stack_framet());
    stack_framet &frame=call_stack.top();

    frame.return_PC=next_PC;
    frame.return_function=function;
    frame.old_stack_pointer=stack_pointer;
    frame.return_value_address=return_value_address;

    // local variables
    std::set<irep_idt> locals;
    get_local_identifiers(f_it->second, locals);

    for(const auto &id : locals)
    {
      std::string local_var = id2string(id);

      const symbolt &symbol=ns.lookup(id);
      unsigned size=get_size(symbol.type);

      if(size!=0)
      {
        frame.local_map[id]=stack_pointer;

        for(unsigned i=0; i<size; i++) //Siqing: fix a bug in the original code!
        {
          unsigned address=stack_pointer+i;
          if(address>=memory.size())
            memory.resize(address+1);
          memory[address].value=0;
          memory[address].identifier=id;
          memory[address].offset=i;
        }

        stack_pointer+=size;
      }
    }

    // assign the arguments
    const code_typet::parameterst &parameters=
      to_code_type(f_it->second.type).parameters();

    if (replacing_main)
    {
      // so arguments are not initialised.
    }
    else
    {
      if(argument_values.size()<parameters.size())
      throw "not enough arguments";

    for(unsigned i=0; i<parameters.size(); i++)
    {
      const code_typet::parametert &a=parameters[i];
      exprt symbol_expr(ID_symbol, a.type());
      symbol_expr.set(ID_identifier, a.get_identifier());
      assert(i<argument_values.size());
      assign(evaluate_address(symbol_expr), argument_values[i]);
    }
    }

    if (next_line && !next_stop_PC_set)
    {
      next_stop_PC = next_PC;
      next_stop_PC_set = true;
    }

    // set up new PC
    function=f_it;
    next_PC=f_it->second.body.instructions.begin();
    
    show_function_start_msg();

    running = true;
  }
  else
    throw "no body for "+id2string(identifier);
}

void interpretert::build_memory_map()
{
  memory_map.clear();

  // put in a dummy for NULL
  memory.resize(1);
  memory[0].offset=0;
  memory[0].identifier="NULL-OBJECT";

  // now do regular static symbols
  for(const auto &s : symbol_table.symbols)
    build_memory_map(s.second);

  // for the locals
  stack_pointer=memory.size();
}

void interpretert::build_memory_map(const symbolt &symbol)
{
  unsigned size=0;

  if(symbol.type.id()==ID_code)
  {
    size=1;
  }
  else if(symbol.is_static_lifetime)
  {
    size=get_size(symbol.type);
  }

  if(size!=0)
  {
    unsigned address=memory.size();

    memory.resize(address+size);
    memory_map[symbol.name]=address;

    for(unsigned i=0; i<size; i++)
    {
      memory_cellt &cell=memory[address+i];
      cell.identifier=symbol.name;
      cell.offset=i;
      cell.value=0;
    }
  }
}

unsigned interpretert::get_size(const typet &type) const
{
  if(type.id()==ID_struct)
  {
    const struct_typet::componentst &components=
      to_struct_type(type).components();

    unsigned sum=0;

    for(const auto &comp : components)
    {
      const typet &sub_type=comp.type();

      if(sub_type.id()!=ID_code)
        sum+=get_size(sub_type);
    }

    return sum;
  }
  else if(type.id()==ID_union)
  {
    const union_typet::componentst &components=
      to_union_type(type).components();

    unsigned max_size=0;

    for(const auto &comp : components)
    {
      const typet &sub_type=comp.type();

      if(sub_type.id()!=ID_code)
        max_size=std::max(max_size, get_size(sub_type));
    }

    return max_size;
  }
  else if(type.id()==ID_array)
  {
    const exprt &size_expr=static_cast<const exprt &>(type.find(ID_size));

    unsigned subtype_size=get_size(type.subtype());

    mp_integer i;
    if(!to_integer(size_expr, i))
      return subtype_size*integer2unsigned(i);
    else
      return subtype_size;
  }
  else if(type.id()==ID_symbol)
  {
    return get_size(ns.follow(type));
  }
  else
    return 1;
}

void interpreter(
  const symbol_tablet &symbol_table,
  const goto_functionst &goto_functions)
{
  interpretert interpreter(symbol_table, goto_functions);
  interpreter();
}
