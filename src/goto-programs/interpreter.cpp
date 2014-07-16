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

#include <util/std_types.h>
#include <util/symbol_table.h>

#include "interpreter_class.h"

void interpretert::operator()()
{
  done=false;
  while (!done)
  {
    run_upto_main = false;
    main_called = false;
    restart = false;

  build_memory_map();

  fix_argc();

  const goto_functionst::function_mapt::const_iterator
    main_it=goto_functions.function_map.find(goto_functionst::entry_point());

  if(main_it==goto_functions.function_map.end())
    throw "main not found";

  const goto_functionst::goto_functiont &goto_function=main_it->second;

  if(!goto_function.body_available())
    throw "main has no body";

  PC=goto_function.body.instructions.begin();
  function=main_it;

    while(!done && !restart)
  {
    show_state();
    run_current_stmt = true;
    command();
    if(!done && !restart && run_current_stmt)
      step();
  }
  }
}

void interpretert::show_state()
{
  std::cout << "\n----------------------------------------------------\n";

  if(PC==function->second.body.instructions.end())
  {
    std::cout << "End of function `"
              << function->first << "'\n";
  }
  else
    function->second.body.output_instruction(
      ns, function->first, std::cout, PC);

  std::cout << '\n';
}

void interpretert::command()
{
  #define BUFSIZE 100

  if (run_upto_main && !main_called)
	  return;

  char command[BUFSIZE];  
  bool cmd_ok = false;
  while (!cmd_ok)
  {
  std::cout << std::endl << "\tCommand (q to quit; h for help): ";

    if(fgets(command, BUFSIZE-1, stdin)==nullptr)
  {
    done=true;
    return;
  }

  std::cout << std::endl;

    parse_cmd_tokens(command);

    cmd_ok = true;
    if (cmd_tokens.size() == 0)
    {
      // press ENTER - next
    }
    else if (cmd_tokens.front() == "quit" || cmd_tokens.front() == "q")
    {
      done=true;
      run_current_stmt = false;
    }
    else if (cmd_tokens.front() == "help" || cmd_tokens.front() == "h" || cmd_tokens.front() == "?")
  {
	  show_help();
    run_current_stmt = false;
  }
    else if (cmd_tokens.front() == "main" || cmd_tokens.front() == "m")
    {
    run_upto_main = true;
    }
    else if (cmd_tokens.front() == "restart" || cmd_tokens.front() == "r")
    {
    restart = true;
    }
    else if (cmd_tokens.front() == "print" || cmd_tokens.front() == "p")
    {
      run_current_stmt = false;
      if (cmd_tokens.size() == 1)
      {
        //std::cout << std::endl << "\tNo variable name provided after the 'print' command";
        //cmd_ok = false;
        print_variable_value("");
      }
      else
      {
        for(int i = 1; i < cmd_tokens.size(); i++){
          print_variable_value(cmd_tokens[i]);
        }
      }
    }
    else
    {
      cmd_ok = false;
    }
  }
}

void interpretert::print_variable_value(const std::string variable) const
{
  // 1. check local variable
  const stack_framet &frame = call_stack.top();
  std::set<irep_idt> locals;
  get_local_identifiers(function->second, locals);
  for(std::set<irep_idt>::const_iterator
      it = locals.begin();
      it != locals.end();
      it++)
  {
    const irep_idt &id = *it;      
    const symbolt &symbol = ns.lookup(id);
    unsigned size = get_size(symbol.type);

    if (frame.local_map.find(id) != frame.local_map.end())
    {
      unsigned address = frame.local_map.find(id)->second;
      std::vector<mp_integer> tmp;
      tmp.resize(size);
      read(address, tmp);
      if (size == 1)
      {
        std::cout << symbol.base_name <<":" << tmp[0] << std::endl;
      }
      else
      {
        std::cout << symbol.base_name <<":" << "<not implemented>" << std::endl;
      }
    }

    //symbol.base_name
    //std::cout << id2string(id) << std::endl;
  }
}

void interpretert::parse_cmd_tokens(const char* cmdline)
  {
  #define SPACE ' '

  cmd_tokens.clear();

  std::string line(cmdline);

  size_t pos;

  // remove LF if any
  pos = line.find_last_of('\n');
  if (pos != std::string::npos)
  {
    line.resize(line.size() - 1);
  }

  // remove CR if any
  pos = line.find_last_of('\r');
  if (pos != std::string::npos)
  {
    line.resize(line.size() - 1);
  }

  pos = line.find_first_not_of(SPACE);
  while (pos != std::string::npos)
  {
    size_t sp_pos = line.find_first_of(SPACE, pos);
    if (sp_pos == std::string::npos)
    {
      cmd_tokens.push_back(line.substr(pos));
      pos = sp_pos;
    }
    else
    {
      cmd_tokens.push_back(line.substr(pos, sp_pos - pos));
      pos = line.find_first_not_of(SPACE, sp_pos + 1);
    }
  }
}

void interpretert::show_help()
{
      std::cout << "\tq - quit" << std::endl
                << "\th - help" << std::endl
              << "\tr - restart" << std::endl
              << "\tm - run until the main" << std::endl
                << "\tENTER - next line" << std::endl;
}

void interpretert::step()
{
  if(PC==function->second.body.instructions.end())
  {
    if(call_stack.empty())
      done=true;
    else
    {
      PC=call_stack.top().return_PC;
      function=call_stack.top().return_function;
      stack_pointer=call_stack.top().old_stack_pointer;
      call_stack.pop();
    }

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
      std::cout << "** assigning " << cell.identifier
                << "[" << cell.offset << "]:=" << rhs[i] << '\n';
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

  const goto_functionst::function_mapt::const_iterator f_it=
    goto_functions.function_map.find(identifier);

  if(f_it==goto_functions.function_map.end())
    throw "failed to find function "+id2string(identifier);

  if (!main_called && (id2string(identifier) == "c::main"))
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

  argument_values.resize(function_call.arguments().size());

  for(std::size_t i=0; i<function_call.arguments().size(); i++)
    evaluate(function_call.arguments()[i], argument_values[i]);

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
      const symbolt &symbol=ns.lookup(id);
      unsigned size=get_size(symbol.type);

      if(size!=0)
      {
        frame.local_map[id]=stack_pointer;

        for(unsigned i=0; i<size; i++)
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

    // set up new PC
    function=f_it;
    next_PC=f_it->second.body.instructions.begin();
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

void interpretert::fix_argc()
{
  const symbolt *symbol;
  if (ns.lookup("c::argc'", symbol))
    return;

  // argc appears in the main(), set value to 1; otherwise assume will fail
  unsigned address = memory_map[symbol->name];
  unsigned size = get_size(symbol->type);
  memory_cellt &cell=memory[address];
  cell.value = 1;
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
