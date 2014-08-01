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

#include <util/cprover_prefix.h>
#include <util/fixedbv.h>
#include <util/ieee_float.h>
#include <util/std_types.h>
#include <util/symbol_table.h>

#include <ansi-c/expr2c_class.h>
#include <ansi-c/literals/convert_float_literal.h>
#include <ansi-c/literals/convert_integer_literal.h>
#include <ansi-c/literals/convert_string_literal.h>

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
    initial_function = function;

    show_function_start_msg();

    next_stop_PC = (initial_function->second).body.instructions.end();
    next_stop_PC_set = false;
    step_out = false;
    completed = false;

    while(!done && !restart)
  {
    show_state();
    command();
    if(!done && !restart && !completed)
      step();
  }
  }
}

void interpretert::show_state() const
{
  show_state(false);
}

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
  if (!completed && (batch_mode || next_stop_PC_set))
  {
    if (!break_point->has_breakpoint_at(PC)) return;
  }

  step_out = false;
  next_line = false;

  if (run_upto_main && !main_called)
	  return;

  bool keep_asking = true;
  while (keep_asking)
  {
	  #define BUFSIZE 100

	  char command[BUFSIZE];  
	  //std::cout << std::endl << ":";
	  std::cout << ":";
	  if (fgets(command, BUFSIZE - 1, stdin) == NULL)
    {
      done = true;
      return;
    }

		keep_asking = false;

    cmd.parse(command);

    if (cmd.is_break())
    {
      manage_breakpoint();
      keep_asking = true;
    }
    else if (cmd.is_callstack())
    {
      show_callstack();
      keep_asking = true;
    }
    else if (cmd.is_function())
    {
      set_entry_function(cmd.get_first_parameter());
      keep_asking = true;

    }
    else if (cmd.is_go())
    {
      if (!completed)
      {
        batch_mode = true;
      }
    }
		else if (cmd.is_next_line())
    {
      next_line = true;
      batch_mode = false;
    }
		else if (cmd.is_quit())
    {
      done=true;
    }
		else if (cmd.is_help())
  {
			cmd.print_help();
			keep_asking = true; 
  }
    else if (cmd.is_main())
    {
    run_upto_main = true;
    }
    else if (cmd.is_modify())
    {
      keep_asking = true;
      modify_variable();
    }
		else if (cmd.is_restart())
    {
    restart = true;
    }
		else if (cmd.is_print())
    {
			keep_asking = true; 
			if (running)
      {
				print();
        }
			else
			{
				show_require_running_msg();
			}
      }
		else if (cmd.is_step_into()) //step into
    {
      batch_mode = false;
      // ok  
    }
		else if (cmd.is_step_out()) //step out
    {
      batch_mode = false;
      step_out = true;
    }
    else if (cmd.is_list())
    {
			keep_asking = true;
      int before_lines = cmd.list_before_lines();
      int after_lines = cmd.list_after_lines();

      list_src(before_lines, after_lines);
    }
    else if (cmd.is_where())
    {
      show_state(true);
			keep_asking = true; 
    }
    else if (cmd.is_silent())
    {
      bool new_silent = cmd.has_silent_on();
      if (silent && !new_silent)
      {
        show_state(true);
      }
      silent = new_silent;
			keep_asking = true; 
    }
    else
    {
			keep_asking = true; 
    }
  }
}

void interpretert::set_entry_function(std::string new_entry_function)
{
  if (new_entry_function.size() == 0)
  {
    std::cout << "funciton name not specified" << std::endl;
    return;
  }
  else if (find_function(new_entry_function) != goto_functions.function_map.end())
  {
    if (new_entry_function.find("main") == 0 || new_entry_function.find(CPROVER_PREFIX) == 0)
    {
      std::cout << new_entry_function 
        << " is an internal function and can't be specified as an entry function." 
        << std::endl;
    }
    else
    {
      entry_function = new_entry_function;
      std::cout << "The entry function has been set to " << entry_function
        << ". Next time when the main function is to be called, this function will be called instead."
        << std::endl;
    }
  }
  else
  {
    std::cout << new_entry_function << " is not a function." << std::endl;
  }
}

void interpretert::modify_variable()
{
  std::vector<std::string> params;
  cmd.get_parameters(params);
  if (params.size() != 2)
  {
    std::cout << "Invalid argument." << std::endl;
    std::cout << "Usage: modify variable_name new_value" << std::endl;
    return;
  }

  std::string var_name = params[0];
  std::string var_value = params[1];

  const symbolt &symbol = get_variable_symbol(var_name);

  if (&symbol == &null_symbol)
  {
    std::cout << "Variable \"" << var_name <<"\" not found" <<  std::endl;
    return;
  }

  if (is_internal_global_varialbe(var_name))
  {
    std::cout << "\"" << var_name <<"\" can't be modified as it is an internal vairable" <<  std::endl;
    return;
  }
  
  if (!symbol.is_lvalue)
  {
    std::cout << "\"" << var_name <<"\" can't be modified" <<  std::endl;
    return;
  }

  //std::string type = symbol.type.pretty();
  //std::cout << type;

  //irept::named_subt::const_iterator c_type = symbol.type.get_comments().find(ID_C_c_type);
  //if (c_type == symbol.type.get_comments().end())
  //{
  //  std::cout << "\"" << var_name <<"\" is not of c type. Currently only c type variable is supported" <<  std::endl;
  //  return;
  //}
  //irept i = c_type->second;
  //std::cout << "c type: " << c_type->second.id() << std::endl;
  //
  // c type example
  // ID_signed_int

  //irept::named_subt::const_iterator const_id = symbol.type.get_comments().find(ID_C_constant);
  //if (const_id != symbol.type.get_comments().end())
  //{
  //  if (const_id->second.id_string() == "1") //if (id2string(const_id->second.id()) == "1")
  //  {
  //    std::cout << "\"" << var_name <<"\" can't be modified as it is a constant vairable" <<  std::endl;
  //    return;
  //  }
  //}

  if (symbol.type.get_bool(ID_C_constant))
  {
    std::cout << "\"" << var_name <<"\" can't be modified as it is a constant vairable" <<  std::endl;
  }
  else if (symbol.type.id() == ID_signedbv || symbol.type.id() == ID_unsignedbv) //also include char
  {
    exprt expr = convert_integer_literal(var_value);
    modify_variable(symbol, expr);
  }
  else if (symbol.type.id() == ID_floatbv)
  {
    exprt expr = convert_float_literal(var_value);
    modify_variable(symbol, expr);
  }
  else
  {
    std::cout << "\"" << var_name <<"\" has a data type currently not supported" <<  std::endl;
    //std::cout << symbol <<  std::endl; //testing. ID_floatbv
  }
}

void interpretert::modify_variable(const symbolt &symbol, const exprt &expr)
{
  std::vector<mp_integer> values;
  evaluate(expr, values);

  if (values.size() > 0)
  {
    exprt symbol_expr(ID_symbol, symbol.type);
    symbol_expr.set(ID_identifier, symbol.name);
    
    unsigned size = get_size(symbol_expr.type());
    if (size == values.size()) //TODO: less than should be ok
    {
      mp_integer address = evaluate_address(symbol_expr);
      assign(address, values);

      // todo
    }
    else
    {
      std::cout << "!! failed to modify" << std::endl;
    }
  }
}

goto_functionst::function_mapt::const_iterator interpretert::find_function(std::string func_name) const
{
  std::string fname = "c::" + func_name;
  for(goto_functionst::function_mapt::const_iterator 
    it = goto_functions.function_map.begin();
    it != goto_functions.function_map.end();
  it++)
  {
    std::string cur_fname = id2string(it->first);
    if (fname == cur_fname) 
    {
      return it;
    }
  }

  return goto_functions.function_map.end();
}

void interpretert::print() const
    {
	std::vector<std::string> parameters;
	cmd.get_parameters(parameters);

  if (cmd.has_print_locals() || (!cmd.has_options() && parameters.empty()))
  {
    print_local_variables(false, true);
  }

  if (cmd.has_print_parameters())
  {
    print_local_variables(true, false);
  }

  if (cmd.has_print_globals())
	{
    print_global_varialbes();
    }

  for(unsigned i = 0; i < parameters.size(); i++)
	{
	  print_variable(parameters[i]);
	}
}

void interpretert::print_local_variables(bool include_args, bool include_real_locals) const
{
  const code_typet::argumentst &arguments=
      to_code_type(function->second.type).arguments();

  std::set<irep_idt> arg_ids;
  for(unsigned i=0; i<arguments.size(); i++)
  {
    const code_typet::argumentt &a=arguments[i];
    const irep_idt &id = a.get_identifier();
    arg_ids.insert(id);
  }

  std::set<irep_idt> locals;
  get_local_identifiers(function->second, locals);

  const stack_framet &frame = call_stack.top();

  // 'locals' contains arguments + local
  for(std::set<irep_idt>::const_iterator
      it = locals.begin();
      it != locals.end();
      it++)
    {
    const irep_idt &id = *it; 
    bool is_arg = arg_ids.find(id) != arg_ids.end();
    if ((include_args && is_arg) || (include_real_locals && !is_arg))
    {
      if (frame.local_map.find(id) != frame.local_map.end())
		{
        const symbolt &symbol = ns.lookup(id);
        print_variable(id2string(symbol.base_name), symbol);
      }
		}
    }
  }

bool interpretert::is_internal_global_varialbe(const std::string name) const
{
  return (name.find(CPROVER_PREFIX) == 0) || (name == "c::argc'") || (name == "c::argv'");
}

void interpretert::print_global_varialbes() const
{
  for(symbol_tablet::symbolst::const_iterator
    it = symbol_table.symbols.begin();
    it != symbol_table.symbols.end();
    it++)
  {
    const symbolt &symbol = it->second;
    std::string var = id2string(symbol.name);
    if (symbol.is_static_lifetime && 
        !is_internal_global_varialbe(var))
	  {
      remove_global_varialbe_prefix(var);
      print_variable(var, symbol);
	  }
  }
}

void interpretert::remove_global_varialbe_prefix(std::string &name) const
{
  if (name.find("c::") == 0)
  {
     name = name.substr(3);
  }
}

const symbolt &interpretert::get_variable_symbol(const std::string variable) const
{
  // 'locals' contains arguments + local
  std::set<irep_idt> locals;
  get_local_identifiers(function->second, locals);

  const stack_framet &frame = call_stack.top();

  // 'locals' contains arguments + local
  for(std::set<irep_idt>::const_iterator
      it = locals.begin();
      it != locals.end();
      it++)
  {
    const irep_idt &id = *it;      
    if (frame.local_map.find(id) != frame.local_map.end())
    {
      const symbolt &symbol = ns.lookup(id);
      std::string name = id2string(symbol.base_name);
      if (name == variable)
      {
        return symbol;
      }
    }
  }

  //global
  for(symbol_tablet::symbolst::const_iterator
    it = symbol_table.symbols.begin();
    it != symbol_table.symbols.end();
    it++)
      {
    const symbolt &symbol = it->second;
    std::string cur_var = id2string(symbol.name);
    if (symbol.is_static_lifetime && 
        !is_internal_global_varialbe(cur_var))
	  {
      remove_global_varialbe_prefix(cur_var);
      if (cur_var == variable)
      {
        return symbol;
      }
      }
    }

 return null_symbol;
}

void interpretert::print_variable(const std::string variable) const
{
  const symbolt &symbol  = get_variable_symbol(variable);
  if (&symbol != &null_symbol)
  {
    print_variable(variable, symbol);
  }
  else
  {
  std::cout << variable <<" - " << "<not found>" << std::endl;
}
}

void interpretert::print_variable(const std::string display_name, const symbolt &symbol) const
{
  exprt symbol_expr(ID_symbol, symbol.type);
  symbol_expr.set(ID_identifier, symbol.name);
      
  std::vector<mp_integer> tmp;
  evaluate(symbol_expr, tmp);

  if (tmp.size() == 1)
  {
    const irep_idt id = symbol.type.get(ID_C_c_type);
    if (id == ID_char || id == ID_signed_char || id == ID_unsigned_char)
    {
      int c = int(tmp[0].to_long());
      if (isprint(c))
      {
        std::cout << display_name <<": '" << char(c) << "'" << std::endl;
      }
      else
      {
        std::cout << display_name <<": " << tmp[0] << std::endl;
      }
    }
    else
    {
    std::cout << display_name <<": " << tmp[0] << std::endl;
  }
  }
  else if (tmp.size() > 1 && symbol.type.id() == ID_array)
  {
    const irep_idt id = symbol.type.subtype().get(ID_C_c_type);
    if (id == ID_char || id == ID_signed_char || id == ID_unsigned_char)
    {
      std::string s = read_string(tmp);
      std::cout << display_name <<": \"" << s << "\"" << std::endl;
    }
    else
  {
    std::cout << display_name <<": {" << tmp[0];
    for(unsigned i = 1; i < tmp.size(); i++)
    {
      std::cout << ", " << tmp[i];
    }

    std::cout << "}" << std::endl;
  }
  }
  else if (symbol_expr.id() == ID_symbol && ns.follow(symbol.type).id() == ID_struct)
  {
    std::cout << display_name <<": ";
    unsigned offset = 0;
    print_struct(ns.follow(symbol.type), tmp, offset);
    std::cout << std::endl;
  }
}

void interpretert::print_struct(const typet &type, const std::vector<mp_integer> values, unsigned &offset) const
{
  if (type.id() == ID_struct)
  {
    std::cout << "{";

    bool first = true;

    const irept::subt &components=
      type.find(ID_components).get_sub();

    forall_irep(it, components)
    {
      const typet &sub_type=static_cast<const typet &>(it->find(ID_type));

      if(sub_type.id() != ID_code)
      {
        std::string field_name = it->get_string(ID_name);

        if (!first) 
        {
          std::cout <<", ";
        }
        else
        {
          first = false;
        }
        std::cout << field_name << ": ";
          print_struct(sub_type, values, offset);
        }
    }

    std::cout << "}";
  }
  else if(type.id() == ID_array)
  {
    const exprt &size_expr = static_cast<const exprt &>(type.find(ID_size));

    mp_integer i;

    unsigned subtype_count;
    if (!to_integer(size_expr, i))
      subtype_count = integer2long(i);
  else
      subtype_count = 1;

    const irep_idt id = type.subtype().get(ID_C_c_type);
    if (id == ID_char || id == ID_signed_char || id == ID_unsigned_char)
  {
      std::vector<mp_integer> tmp;
      for (unsigned i = 0; i < subtype_count; i++)
      {
        tmp.push_back(values[i + offset]);
      }

      std::string s = read_string(tmp);
      std::cout << "\"" << s << "\"";

      offset += subtype_count;
        }
    else
    {
      std::cout << "{";

      const typet sub_type = type.subtype();
      for (unsigned i = 0; i < subtype_count; i++)
      {
        if (i != 0)
        {
          std::cout <<", ";
      }

        print_struct(sub_type, values, offset);
    }

    std::cout << "}";
  }
}
  else if(type.id() == ID_symbol)
  {
    print_struct(ns.follow(type), values, offset);
  }
  else
  {
    std::cout << values[offset];
    offset++;
  }
}

void interpretert::list_src(int before_lines, int after_lines) const
{
  goto_programt::const_targett cur_PC = PC;
  goto_programt::const_targett start_PC = (function->second).body.instructions.begin();
  goto_programt::const_targett end_PC = (function->second).body.instructions.end();

  int before = 0;
  while (before_lines > 0 && cur_PC != start_PC)
  {
    cur_PC--;
    before++;
  }

  for(unsigned i = 0; i < before && cur_PC != PC && cur_PC != end_PC; i++)
  {
    function->second.body.output_instruction(ns, function->first, std::cout, cur_PC);
    std::cout << std::endl;

    cur_PC++;
  }
  
  cur_PC = PC;
  for(unsigned i = 0; i < after_lines && cur_PC != end_PC; i++)
  {
    function->second.body.output_instruction(ns, function->first, std::cout, cur_PC);
    std::cout << std::endl;

    cur_PC++;
  }
}

void interpretert::show_callstack() const
{
  if (call_stack.empty()) 
  {
    std::cout << "Empty Callstack" << std::endl;
    return;
  }

  std::cout << std::endl << function->first << "'" << std::endl;
  function->second.body.output_instruction(ns, function->first, std::cout, PC);

  call_stackt tmp_call_stack(call_stack);

  while (!tmp_call_stack.empty())
  {
    const goto_functionst::function_mapt::const_iterator 
      caller_function = tmp_call_stack.top().return_function;
    const goto_programt::const_targett caller_PC = (tmp_call_stack.top().return_PC)--;

    std::cout << std::endl << caller_function->first << "'" << std::endl;
    caller_function->second.body.output_instruction(ns, caller_function->first, std::cout, caller_PC);

    tmp_call_stack.pop();
  }
}

void interpretert::manage_breakpoint()
{
  if (completed) return;
  
  if (cmd.has_breakpoint_remove_all())
  {
    break_point->remove_all();
  }
  else if (cmd.has_breakpoint_toggle())
  {
    break_point->toggle_breakpoint(PC);
  }
  else if (cmd.has_breakpoint_remove())
  {
    std::string line_no = cmd.get_breakpoint_lineno();
    if (line_no == "")
    {
      break_point->remove_breakpoint(PC);
    }
    else
    {
      std::string module = cmd.get_breakpoint_module();
      if (module == "") module = get_current_module();

      if (module != "")
        break_point->remove_breakpoint(line_no, module);
    }
  }
  else if (cmd.has_breakpoint_add())
  {
    std::string line_no = cmd.get_breakpoint_lineno();
    if (line_no == "")
    {
      if (cmd.has_breakpoint_add()) 
        break_point->add_breakpoint(PC);
    }
    else
    {
    std::string module = cmd.get_breakpoint_module();
      if (module == "") module = get_current_module();

      if (module != "")
        break_point->add_breakpoint(line_no, module);
  }
  }
  else if (cmd.has_breakpoint_list())
  {
    break_point->list();
  }
  }

std::string interpretert::get_current_module() const
{
  goto_programt::const_targett begin_PC = (function->second).body.instructions.begin();
  return begin_PC->location.is_nil() ? "" : id2string(begin_PC->location.get_file());
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
      //done=true;
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

void interpretert::reset_next_PC()
{
  if (next_stop_PC_set && (PC->function == next_stop_PC->function) && (PC == next_stop_PC))
  {
    next_stop_PC_set = false;
    next_stop_PC = (initial_function->second).body.instructions.end();
    
    step_out = false;
    next_line = false;
  }
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
  else if(statement==ID_printf)
  {
    execute_printf();
  }
  else
    throw "unexpected OTHER statement: "+id2string(statement);
}

/// execute printf() - work for integer/float/double/string/char
/// TODO: move this to interpreter_util.cpp
bool is_c_pointer_of_char(typet type)
{
  if (type.id() == ID_pointer)
  {
    const irep_idt id = type.subtype().get(ID_C_c_type);
    if (id == ID_char || id == ID_signed_char || id == ID_unsigned_char)
    {
      return true;
    }
  }

  return false;
}

>>>>>>> 27ad2cb0c... fixed that "char msg[] = "hello"; printf(msg);" was not working. Now works - a lot of experiments!
void interpretert::execute_printf() const
{
  codet src = PC->code;
  if (!src.has_operands()) return;

  exprt::operandst::const_iterator it=src.operands().begin();
  const exprt &expr = *it;

  // The first argument is always a string (see printf doc),
  // and the safiest way to get the string value is to call expr..
  std::string first_arg = "";

  if (expr.id() == ID_address_of && is_c_pointer_of_char(expr.type()))
  {
    if (expr.has_operands() && 
      expr.operands().size() == 1 && 
      expr.op0().id() == ID_index && 
      expr.op0().operands().size() ==2)
    {
      const index_exprt index_expr = static_cast<const index_exprt &>(expr.op0()); 
      const exprt arr_expr = index_expr.op0();
      if (arr_expr.type().id() == ID_array)
      {
        if (arr_expr.id() == ID_symbol || arr_expr.id() == ID_string_constant)
        {
          //This address the following scenarios: 
          //      char msg[] = "Hello"; 
          //      printf(msg);      // arr_expr.id() == ID_symbol
          //      printf("Hello");  // arr_expr.id() == ID_string_constant
          std::vector<mp_integer> dest;
          evaluate(arr_expr, dest);
          first_arg = read_string(dest);
        }
      }
    }
  }

  if (first_arg.size() == 0)
  {
        std::vector<mp_integer> tmp;
  evaluate(expr, tmp);
  first_arg = read_string(tmp);
  }

  if (first_arg.size() == 0) return;

  size_t p = 0;
  const size_t len = first_arg.size();
  while (p < len)
  {
    size_t p1 = first_arg.find_first_of("%", p);
    if (p1 == std::string::npos)
  {
      // no '%' any more
      std::cout << first_arg.substr(p);
      break;
  }

    if (p1 + 1 < len && first_arg[p1 + 1] == '%')
  {
      std::cout << first_arg.substr(p, p1 + 1);
      p = p1 + 2;
      continue;
  }

    // % found, and there is no % immediately after i
    if (it != src.operands().end()) it++; 

    if (it == src.operands().end()) 
      throw "Invalid printf format string found - argument count doesn't match %";

    // trying to find next %
    size_t p2 = first_arg.find_first_of("%", p1 + 1);
    if (p2 == std::string::npos)
  {
      std::string s = first_arg.substr(p);
      print_arg(s, *it);
      break;
  }
  else
  {
      std::string s = first_arg.substr(p, p2 - p - 1);
      p = p2;
      print_arg(s, *it);
      }
      }
    }

void interpretert::print_arg(const std::string str_format, const exprt &expr) const
    {
  std::vector<mp_integer> tmp;
  evaluate(expr, tmp);

  if (tmp.size() == 0) return;

  if (expr.type().id() == ID_signedbv) //this also includes char
    {
    signed x = tmp[0].to_long();
    printf(str_format.c_str(), x);

    return;
  }
  else if (expr.type().id() == ID_unsignedbv)
      {
    unsigned x = tmp[0].to_ulong();
    printf(str_format.c_str(), x);

    return;
  }
  else if (expr.type().id() == ID_floatbv)
  {
    ieee_floatt f;
    f.spec = to_floatbv_type(expr.type());
    f.unpack(tmp[0]);
    //f.from_expr(to_constant_expr(expr));

    //std::cout << f.to_string_decimal(10) << std::endl; //work
    // float x = f.to_float(); //working when printf("Hello World, msg=%f\n", 10.23f);
    // must check is_float() or is_double()
    if (f.is_float())
        {
      float x = f.to_float(); //not working when printf("Hello World, msg=%f\n", 10.23); //working for 10.23f
      printf(str_format.c_str(), x);
        }
    else if (f.is_double())
    {
      double x = f.to_double();
      printf(str_format.c_str(), x);
      }

    return;
    }
  else
  {
    // assume string now.
    std::string arg_value = read_string(tmp);
    printf(str_format.c_str(), arg_value.c_str());
  }
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

void interpretert::show_function_start_msg() const
{
  if (silent) return;

    std::cout << std::endl;
    std::cout << "----------------------------------------------------"
              << std::endl;
  std::cout << "Start of function '"
              << function->first << "'" << std::endl;
}

void interpretert::show_require_running_msg() const
{
	std::cout << std::endl;
  std::cout << "This command is unavailable as the goto binary is not running";
	std::cout << std::endl;
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
