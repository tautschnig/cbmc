/*******************************************************************\

Module: Command Processing for GOTO Interpreter

Author: Siqing Tang, jtang707@gmail.com

\*******************************************************************/

#include <ctype.h>
#include <stdio.h>
#include <cctype>
#include <cstdio>
#include <iostream>
#include <fstream>

#include <util/cprover_prefix.h>
#include <util/fixedbv.h>
#include <util/ieee_float.h>
#include <util/std_types.h>
#include <util/symbol_table.h>

#include <ansi-c/expr2c_class.h>
#include <ansi-c/literals/convert_float_literal.h>
#include <ansi-c/literals/convert_integer_literal.h>
#include <ansi-c/literals/convert_string_literal.h>

#include "interpreter.h"
#include "interpreter_class.h"

#define BUFSIZE 100

/*******************************************************************\

Function: interpretert::command

Inputs:

Outputs:

Purpose:

\*******************************************************************/

void interpretert::command()
{
  if (!completed && (batch_mode || next_stop_PC_set || reading_from_queue))
  {
    bool has_breakpoint = break_point->has_breakpoint_at(PC);
    if (!has_breakpoint) return;
    if (reading_from_queue)
    {
      reading_from_queue = false;
      while (!queued_commands.empty())
      {
        queued_commands.pop();
      }
    }
  }

  step_out = false;
  next_line = false;

  if (run_upto_main && !main_called)
    return;

  bool keep_asking = true;
  while (keep_asking)
  {
    char command[BUFSIZE];  
    //std::cout << std::endl << ":";
    std::cout << ":";

    keep_asking = false;
    bool bad_command = false;

    if (reading_from_queue && !queued_commands.empty())
    {
      std::string cmd_line = queued_commands.front();
      queued_commands.pop();
      if (queued_commands.empty()) reading_from_queue = false;
      cmd.parse(cmd_line.c_str());
    }
    else
    {
      if (fgets(command, BUFSIZE - 1, stdin) == NULL)
      {
        done = true;
        return;
      }
      cmd.parse(command);
    }

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
      done = true;
    }
    else if (cmd.is_help())
    {
      std::string help_cmd = cmd.get_first_parameter();
      if (help_cmd == "")
        cmd.help();
      else
        cmd.help(help_cmd);

      keep_asking = true; 
    }
    else if (cmd.is_load())
    {
      if (!reading_from_queue)
      {
        keep_asking = true;
        load_commands_from_file();
      }
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
    else if (cmd.is_save())
    {
      keep_asking = true;
      save_commands();
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
    else if (cmd.is_watch())
    {
      keep_asking = true;
      manage_watch();
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
      bad_command = true;
      std::cout << "bad command '" << cmd.get_command() << "'" << std::endl;
      keep_asking = true; 
    }

    if (!bad_command && !cmd.is_save())
    {
      commands.push_back(std::string(cmd.get_command()));
    }
  }
}

/*******************************************************************\

Function: interpretert::set_entry_function

Inputs:

Outputs:

Purpose:

\*******************************************************************/

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

/*******************************************************************\

Function: interpretert::modify_variable

Inputs:

Outputs:

Purpose:

\*******************************************************************/

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

/*******************************************************************\

Function: interpretert::modify_variable

Inputs:

Outputs:

Purpose:

\*******************************************************************/

void interpretert::modify_variable(const symbolt &symbol, const exprt &expr)
{
  std::vector<mp_integer> values;
  evaluate(expr, values);

  if (values.size() > 0)
  {
    exprt symbol_expr(ID_symbol, symbol.type);
    symbol_expr.set(ID_identifier, symbol.name);

    unsigned size = get_size(symbol_expr.type());
    if (size == values.size()) //TODO: less than should be ok for string
    {
      mp_integer address = evaluate_address(symbol_expr);

      if (values.size() == 1 &&
          symbol_expr.type().id() == ID_floatbv && 
          symbol_expr.type().get(ID_C_c_type) == ID_float)
      {
        ieee_floatt f;
        f.spec = to_floatbv_type(expr.type());
        f.unpack(values[0]);
        if (f.is_double())
        {
          // re-pack into float
          float f_val = (float)f.to_double();
          f.from_float(f_val);
          values[0] = f.pack();
        }
      }

      assign(address, values);

      // todo
    }
    else
    {
      std::cout << "!! failed to modify" << std::endl;
    }
  }
}

/*******************************************************************\

Function: interpretert::find_function

Inputs:

Outputs:

Purpose:

\*******************************************************************/

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

/*******************************************************************\

Function: interpretert::print

Inputs:

Outputs:

Purpose:

\*******************************************************************/

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

/*******************************************************************\

Function: interpretert::print_local_variables()

Inputs:

Outputs:

Purpose:

\*******************************************************************/

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

/*******************************************************************\

Function: interpretert::is_internal_global_varialbe

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert::is_internal_global_varialbe(const std::string name) const
{
  return (name.find(CPROVER_PREFIX) == 0) || (name == "c::argc'") || (name == "c::argv'");
}

/*******************************************************************\

Function: interpretert::print_global_varialbes

Inputs:

Outputs:

Purpose:

\*******************************************************************/

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

/*******************************************************************\

Function: interpretert::remove_global_varialbe_prefix

Inputs:

Outputs:

Purpose:

\*******************************************************************/

void interpretert::remove_global_varialbe_prefix(std::string &name) const
{
  if (name.find("c::") == 0)
  {
    name = name.substr(3);
  }
}

/*******************************************************************\

Function: interpretert::get_variable_symbol

Inputs:

Outputs:

Purpose:

\*******************************************************************/

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

/*******************************************************************\

Function: interpretert::print_variable

Inputs:

Outputs:

Purpose:

\*******************************************************************/

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

/*******************************************************************\

Function: interpretert::print_variable

Inputs:

Outputs:

Purpose:

\*******************************************************************/

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
    else if (id == ID_float || id == ID_double)
    {
      ieee_floatt f;
      f.spec = to_floatbv_type(symbol_expr.type());
      f.unpack(tmp[0]);

      if (f.is_float())
      {
        float x = f.to_float(); //working when float = 10f; not working when float x = 10;
        std::cout << display_name <<": " << x << std::endl;
      }
      else if (f.is_double())
      {
        double x = f.to_double();
        std::cout << display_name <<": " << x << std::endl;
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

/*******************************************************************\

Function: interpretert::print_struct

Inputs:

Outputs:

Purpose:

\*******************************************************************/

void interpretert::print_struct(const typet &type, const std::vector<mp_integer> values, unsigned &offset) const
{
  if(type.id() == ID_struct)
  {
    std::cout << "{";

    bool first = true;

    const irept::subt &components=
      type.find(ID_components).get_sub();

    forall_irep(it, components)
    {
      const typet &sub_type=static_cast<const typet &>(it->find(ID_type));

      if(sub_type.id()!=ID_code)
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

/*******************************************************************\

Function: interpretert::list_src

Inputs:

Outputs:

Purpose:

\*******************************************************************/

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

/*******************************************************************\

Function: interpretert::show_callstack

Inputs:

Outputs:

Purpose:

\*******************************************************************/

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

/*******************************************************************\

Function: interpretert::manage_breakpoint

Inputs:

Outputs:

Purpose:

\*******************************************************************/

void interpretert::manage_breakpoint()
{
  if (completed) return;

  if (cmd.has_breakpoint_remove_all())
  {
    break_point->remove_all();
  }
  else if (cmd.has_breakpoint_toggle())
  {
    break_point->toggle(PC);
  }
  else if (cmd.has_breakpoint_remove())
  {
    std::string line_no = cmd.get_breakpoint_lineno();
    if (line_no == "")
    {
      break_point->remove(PC);
    }
    else
    {
      std::string file = cmd.get_breakpoint_file();
      if (file == "") file = get_current_file();

      if (file != "")
        break_point->remove(line_no, file);
    }
  }
  else if (cmd.has_breakpoint_add())
  {
    std::string line_no = cmd.get_breakpoint_lineno();
    if (line_no == "")
    {
      if (cmd.has_breakpoint_add()) 
        break_point->add(PC);
    }
    else
    {
      std::string module = cmd.get_breakpoint_file();
      if (module == "") module = get_current_file();

      if (module != "")
        break_point->add(line_no, module);
    }
  }
  else if (cmd.has_breakpoint_list())
  {
    break_point->list();
  }
}

/*******************************************************************\

Function: interpretert::manage_watch

Inputs:

Outputs:

Purpose:

\*******************************************************************/

void interpretert::manage_watch()
{
  if (completed) return;

  if (cmd.has_watch_remove_all())
  {
    watch->remove_all();
  }
  else if (cmd.has_watch_remove())
  {
    std::vector<std::string> params;
    cmd.get_parameters(params);

    std::string line_no = cmd.get_watch_lineno();
    std::string file = cmd.get_watch_file();
    if (file == "") file = get_current_file();

    if (line_no == "")
    {
      if (params.size() == 0)
        watch->remove(PC);
      else
      {
        if (!validate_watch_variables(file, line_no, params)) return;
        watch->remove(PC, params);
      }
    }
    else
    {
      if (!validate_watch_variables(function->second, params)) return;
      watch->remove(line_no, file, params);
    }
  }
  else if (cmd.has_watch_list())
  {
    watch->list();
  }
  else if (cmd.has_watch_add())
  {
    std::vector<std::string> params;
    cmd.get_parameters(params);
    if (params.size() == 0)
    {
      std::cout << "no watch variable specified" << std::endl;
      return;
    }

    std::string line_no = cmd.get_watch_lineno();
    if (line_no == "")
    {
      if (!validate_watch_variables(function->second, params)) return;

      watch->add(PC, params);
    }
    else
    {
      std::string file = cmd.get_watch_file();
      if (file == "") file = get_current_file();

      if (!validate_watch_variables(file, line_no, params)) return;

      watch->add(line_no, file, params);
    }
  }
}

/*******************************************************************\

Function: interpretert::validate_watch_variables

Inputs: 

Outputs: 

Purpose: validate file/line-no

\*******************************************************************/

bool interpretert::validate_watch_variables(
  std::string file,
  std::string line_no,
  const std::vector<std::string> variables) const
{
  if (variables.size() == 0) return true;

  for(goto_functionst::function_mapt::const_iterator 
    it = goto_functions.function_map.begin();
    it != goto_functions.function_map.end();
    it++)
  {
    const goto_functionst::goto_functiont &goto_function=it->second;
    if (!goto_function.body_available) continue;

    goto_programt::const_targett PC = goto_function.body.instructions.begin();
    if (PC != goto_function.body.instructions.end() && 
        PC->location.is_not_nil() &&
        id2string(PC->location.get_file()) == file)
    {
      while (PC != goto_function.body.instructions.end())
      {
        std::string line = id2string(PC->location.get_line());
        if (line == line_no) 
        {
          return validate_watch_variables(goto_function, variables);
        }
        PC++;
      }
    }
  }

  std::cout << "failed to locate a function at the line-no/file specified" << std::endl;

  return false;
}

/*******************************************************************\

Function: interpretert::validate_watch_variables

Inputs: a list of variables

Outputs: true/false indicating whether variable names are valid.

Purpose: validate variable names.

\*******************************************************************/

bool interpretert::validate_watch_variables(
  const goto_function_templatet<goto_programt> &goto_function,
  const std::vector<std::string> variables) const
{
  if (variables.size() == 0) return true;

  std::set<std::string> accept_vars;

  // 'locals' contains arguments + local
  std::set<irep_idt> locals;
  get_local_identifiers(goto_function, locals);

  for (std::set<irep_idt>::const_iterator l_it = locals.begin();
       l_it != locals.end();
       l_it++)
  {
    const symbolt &symbol = ns.lookup(*l_it);
    std::string name = id2string(symbol.base_name);
    accept_vars.insert(name);
  }

  //global
  for(symbol_tablet::symbolst::const_iterator
    it = symbol_table.symbols.begin();
    it != symbol_table.symbols.end();
    it++)
  {
    const symbolt &symbol = it->second;
    std::string name = id2string(symbol.name);
    if (symbol.is_static_lifetime && 
      !is_internal_global_varialbe(name))
    {
      remove_global_varialbe_prefix(name);
      accept_vars.insert(name);
    }
  }

  bool result = true;

  for(unsigned i = 0; i < variables.size(); i++)
  {
    if (accept_vars.find(variables[i]) == accept_vars.end())
    {
      std::cout << variables[i] << " is not a valid variable" << std::endl;
      result = false;
    }
  }

  return result;
}

/*******************************************************************\

Function: interpretert::show_watches

Inputs:

Outputs:

Purpose:

\*******************************************************************/

void interpretert::show_watches() const
{
  std::vector<std::string> watch_vars;
  watch->get_watch_variables(PC, watch_vars);
  for(unsigned i = 0; i < watch_vars.size(); i++)
  {
    print_variable(watch_vars[i]);
  }
}

/*******************************************************************\

Function: interpretert::get_current_file

Inputs:

Outputs:

Purpose:

\*******************************************************************/

std::string interpretert::get_current_file() const
{
  goto_programt::const_targett begin_PC = (function->second).body.instructions.begin();
  return begin_PC->location.is_nil() ? "" : id2string(begin_PC->location.get_file());
}

/*******************************************************************\

Function: interpretert::save_commands

Inputs:

Outputs:

Purpose:

\*******************************************************************/

void interpretert::save_commands() const
{
  std::string file = cmd.get_first_parameter();
  if (file.empty())
  {
    std::cout << "file name must be specified for the save command" << std::endl;
    return;
  }

  if (!cmd.has_save_overwrite())
  {
    std::ifstream inp_file(file.c_str());
    bool file_exists = inp_file;
    inp_file.close();
    if (file_exists)
    {
      std::cout << "failed to save to '" << file << "'" << " as it already exists." << std::endl;
      std::cout << "if you want to overwrite the file, use the --overwrite option" << std::endl;
      return;
    }
  }

  std::ofstream cmd_file(file.c_str());
  if (cmd_file.is_open())
  {
    for(unsigned i = 0; i < commands.size(); i++)
      cmd_file << commands[i] << std::endl;

    cmd_file.close();
    std::cout << "saved to '" << file << "'" << std::endl;
  }
  else
  {
    std::cout << "unable to save to '" << file << "'" << std::endl;
  }
}

/*******************************************************************\

Function: interpretert::load_commands_from_file

Inputs:

Outputs:

Purpose:

\*******************************************************************/

void interpretert::load_commands_from_file()
{
  std::string file = cmd.get_first_parameter();
  if (file.empty())
  {
    std::cout << "file name must be specified for the load command" << std::endl;
    return;
  }

  FILE *f = fopen(file.c_str(), "r");
  if(f != NULL)
  {
    while (!queued_commands.empty())
    {
        queued_commands.pop();
    }

    char command[BUFSIZE];  
    while (fgets(command, BUFSIZE - 1, f) != NULL)
    {
      std::string line(command);
      queued_commands.push(line);
    }
    fclose(f);

    reading_from_queue = !queued_commands.empty();
  }
  else
  {
    std::cout << "failed to open '" << file << "'" << "." << std::endl;
    return;
  }
}

/*******************************************************************\

Function: interpretert::reset_next_PC

Inputs:

Outputs:

Purpose:

\*******************************************************************/

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

/*******************************************************************\

Function: interpretert::is_c_pointer_of_char

Inputs:

Outputs:

Purpose: execute printf() - work for integer/float/double/string/char

\*******************************************************************/

//TODO: move this to interpreter_util.cpp
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

/*******************************************************************\

Function: interpretert::execute_printf

Inputs:

Outputs:

Purpose:

\*******************************************************************/

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

/*******************************************************************\

Function: interpretert::print_arg()

Inputs:

Outputs:

Purpose:

\*******************************************************************/

void interpretert::print_arg(const std::string str_format, const exprt &expr) const
{
  std::vector<mp_integer> tmp;
  evaluate(expr, tmp);

  if (tmp.size() == 0) return;

  if (expr.type().id() == ID_signedbv) //this also includes char
  {
    signed x = tmp[0].to_long();
    printf(str_format.c_str(), x);
  }
  else if (expr.type().id() == ID_unsignedbv)
  {
    unsigned x = tmp[0].to_ulong();
    printf(str_format.c_str(), x);
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
  }
  else
  {
    // assume string now.
    std::string arg_value = read_string(tmp);
    printf(str_format.c_str(), arg_value.c_str());
  }
}

/*******************************************************************\

Function: interpretert::show_function_start_msg

Inputs:

Outputs:

Purpose:

\*******************************************************************/

void interpretert::show_function_start_msg() const
{
  if (silent) return;

  std::cout << std::endl;
  std::cout << "----------------------------------------------------"
    << std::endl;
  std::cout << "Start of function '"
    << function->first << "'" << std::endl;
}

/*******************************************************************\

Function: interpretert::show_require_running_msg

Inputs:

Outputs:

Purpose:

\*******************************************************************/

void interpretert::show_require_running_msg() const
{
  std::cout << std::endl;
  std::cout << "This command is unavailable as the goto binary is not running";
  std::cout << std::endl;
}



/*******************************************************************\

Function: interpretert::fix_argc

Inputs:

Outputs:

Purpose:

\*******************************************************************/

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
