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
#include <ansi-c/literals/convert_character_literal.h>
#include <ansi-c/literals/convert_string_literal.h>

#include "interpreter.h"
#include "interpreter_class.h"
#include "interpreter_util.h"

#define BUFSIZE 100

/*******************************************************************\

Function: interpretert::command

Inputs:

Outputs:

Purpose:

\*******************************************************************/

void interpretert::command()
{
  if (!completed && (batch_mode || next_stop_PC_set))
  {
    bool has_breakpoint = break_point->has_breakpoint_at(PC);
    if (!has_breakpoint) return;
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
      unsigned before_lines = cmd.list_before_lines();
      unsigned after_lines = cmd.list_after_lines();

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

  std::string var_name_full = params[0];
  std::string var_name = params[0];
  std::string var_value = params[1];
  std::string var_name_suffix = "";
  
  size_t p  = find_next_exp_sep(var_name, 0);
  if (p != std::string::npos)
  {
    var_name_suffix = var_name.substr(p);
    var_name = var_name.substr(0, p);
  }

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

  if (symbol.type.get_bool(ID_C_constant))
  {
    std::cout << "\"" << var_name <<"\" can't be modified as it is a constant vairable" <<  std::endl;
    return;
  }

  exprt to_expr(ID_symbol, symbol.type);
  to_expr.set(ID_identifier, symbol.name);

  if (var_name_suffix != "")
  {
    to_expr = parse_expression(to_expr, var_name, var_name_suffix);
  }

  typet type;
  if (to_expr.type().id() == ID_symbol)
  {
    type = ns.follow(to_expr.type());
  }
  else if (symbol.type.id() == ID_pointer)
  {
    type = symbol.type.subtype();
  }
  else
  {
    type = to_expr.type();
  }

  if (type.id() == ID_signedbv || type.id() == ID_unsignedbv) //also include char
  {
    const irep_idt id = type.get(ID_C_c_type);
    if (id == ID_char)
    {
      int len = var_value.size();
      if (len >= 2 && var_value[0] == '\'' && var_value[len - 1] == '\'') 
      {
        var_value = var_value.substr(1, len - 2);
      }

      if (var_value == "")
      {
        var_value = "'\0'";
      }
      else
      {
        var_value = "'" + var_value.substr(0, 1) + "'";
      }

      exprt expr = convert_character_literal(var_value, false);
      modify_variable(to_expr, expr);
    }
    else
    {
      exprt expr = convert_integer_literal(var_value);
      modify_variable(to_expr, expr);
    }
     return;
  }

  if (type.id() == ID_floatbv)
  {
    exprt expr = convert_float_literal(var_value);
    modify_variable(to_expr, expr);
    return;
  }

  if (type.id() == ID_array)
  {
    if (type.subtype().get(ID_C_c_type) == ID_char)
    {
      int len = var_value.size();
      if (len < 2 || (var_value[0] != '"' || var_value[len - 1] != '"'))
      {
        var_value = "\"" + var_value + "\"";
      }
    
      exprt expr = convert_string_literal(var_value);
      modify_variable(to_expr, expr);
      return;
    }
  }

  std::cout << "\"" << var_name <<"\" has a data type currently not supported" <<  std::endl;
}

/*******************************************************************\

Function: interpretert::parse_expression

Inputs: a string expression

Outputs:

Purpose: parse an expression

\*******************************************************************/

exprt interpretert::parse_expression(
  const exprt &op0,
  const std::string prefix, 
  const std::string suffix) const
{
  if (suffix == "")
  {
    return op0;
  }
  else if (suffix.substr(0, 1) == "[")
  {
    // get array index -> index_expr
    size_t pos = suffix.find_first_of(']', 1);
    std::string index_str;
    std::string new_suffix;
    
    if (pos == std::string::npos)
    {
      new_suffix = "";
      index_str = suffix.substr(1);
    }
    else
    {
      new_suffix = suffix.substr(pos + 1);
      index_str = suffix.substr(1, pos - 1);
    }

    const exprt index_expr = get_array_index(index_str);
    
    unsigned idx;

    std::vector<mp_integer> values;
    evaluate(index_expr, values);
    if (values.size() == 1)
      idx = integer2long(values[0]);
    else
      throw "invalid index";

    std::string new_prefix = prefix + "[" + index_str + "]"; 

    if (op0.type().id() == ID_array)
    {
      // get array size
      const exprt &size_expr = static_cast<const exprt &>(op0.type().find(ID_size));
      unsigned size = 1;
      mp_integer i;

      if(!to_integer(size_expr, i))
        size = integer2long(i);

      // check if index is in range
      if (idx >= size)
        throw "index out of boundary.";

      index_exprt dest_expr;
      dest_expr.array() = op0;
      dest_expr.index() = index_expr;
      dest_expr.type() = op0.type().subtype();
    
      return parse_expression(dest_expr, new_prefix, new_suffix);
    }
    else if (op0.type().id() == ID_pointer)
    {
      if (op0.id() == ID_symbol)
      {
        exprt exp_plus(ID_plus);
        exp_plus.copy_to_operands(op0, index_expr);

        exprt exp(ID_dereference);
        exp.copy_to_operands(exp_plus);

        return parse_expression(exp, new_prefix, new_suffix);
      }
    }
    
    throw "array expected (" + suffix + ") but not found";
  }
  else if (suffix.substr(0, 1) == ".")
  {
    typet type;
    if (op0.type().id() == ID_symbol)
    {
      type = ns.follow(op0.type());
    }
    else if (op0.id() == ID_index)
    {
      type = op0.op0().type().subtype();
      if (type.id() == ID_symbol)
        type = ns.follow(type);
    }
    else
    {
      type = op0.type();
    }

    if (type.id() == ID_struct)
    {
      std::string member_name;
      std::string new_suffix;

      size_t p  = find_next_exp_sep(suffix, 1);
      if (p != std::string::npos)
      {
        member_name = suffix.substr(1, p - 1);
        new_suffix = suffix.substr(p);
      }
      else
      {
        member_name = suffix.substr(1);
        new_suffix = "";
      }

      if (member_name == "")
        throw "member name expected but not found.";

      std::string new_prefix = prefix + "." + member_name;

      const struct_union_typet &stru_type = to_struct_union_type(type);
      const struct_union_typet::componentst &components = stru_type.components();

      for(struct_union_typet::componentst::const_iterator
        it=components.begin();
        it!=components.end();
        ++it)
      {
        std::string component_name = id2string(it->get_name());
        if (component_name == member_name)
        {
          member_exprt member(op0, it->get_name(), it->type());
          return parse_expression(member, new_prefix, new_suffix);
        }
      }

      throw "invalid member name '" + member_name + "'";
    }
    else
    {
      throw "member (.) supported only for struct/union";
    }
  }

  throw "unsupported expression";
}

/*******************************************************************\

Function: interpretert::get_array_index

Inputs:

Outputs:

Purpose:

\*******************************************************************/
exprt interpretert::get_array_index(const std::string index_str) const
{
  if (index_str == "")
    throw "missing index for array";

  if (index_str.find("[") != std::string::npos)
    throw "index must be a variable or an integer constant";

  const symbolt &symbol = get_variable_symbol(index_str);

  if (&symbol == &null_symbol)
  {
    return convert_integer_literal(index_str);
  }
  else if (symbol.type.id() == ID_signedbv || 
           symbol.type.id() == ID_unsignedbv)
  {
    return symbol.symbol_expr();
  }
  else
  {
    throw "variable " + index_str + 
          " is not of an integral type and can't be used as an index";
  }
}

/*******************************************************************\

Function: interpretert::modify_variable

Inputs:

Outputs:

Purpose:

\*******************************************************************/

void interpretert::modify_variable(const exprt &to_expr, const exprt &from_expr)
{
  std::vector<mp_integer> values;
  evaluate(from_expr, values);

  if (values.size() > 0)
  {
    unsigned size = get_size(to_expr.type());
    if (size == values.size())
    {
      mp_integer address = evaluate_address(to_expr);

      if (values.size() == 1 &&
          to_expr.type().id() == ID_floatbv && 
          to_expr.type().get(ID_C_c_type) == ID_float)
      {
        ieee_floatt f;
        f.spec = to_floatbv_type(from_expr.type());
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
    }
    else if (to_expr.type().id() == ID_array)
    {
      mp_integer address = evaluate_address(to_expr);
      assign(address, values);
      if (values.size() > size)
        std::cout << "WARNING: more items than array length are discarded" << std::endl;
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
      return it;
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
    print_local_variables(false, true);

  if (cmd.has_print_parameters())
    print_local_variables(true, false);

  if (cmd.has_print_globals())
    print_global_varialbes();

  for(unsigned i = 0; i < parameters.size(); i++)
    print_variable(parameters[i]);
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
        print_variable(id2string(symbol.base_name), symbol.symbol_expr());
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
      print_variable(var, symbol.symbol_expr());
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
    name = name.substr(3);
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
        return symbol;
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
        return symbol;
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
  std::string var_prefix = variable;
  std::string var_suffix = "";

  size_t p  = find_next_exp_sep(variable, 0);
  if (p != std::string::npos)
  {
    var_suffix = variable.substr(p);
    var_prefix = variable.substr(0, p);
  }

  const symbolt &symbol  = get_variable_symbol(var_prefix);

  if (&symbol != &null_symbol)
  {
    const exprt exp = var_suffix == "" ? 
      symbol.symbol_expr():
      parse_expression(symbol.symbol_expr(), var_prefix, var_suffix);
    print_variable(variable, exp);

    return;
  }

  std::cout << variable 
            <<" is not a valid variable or expression the print command supports" 
            << std::endl;
}

/*******************************************************************\

Function: interpretert::print_variable

Inputs:

Outputs:

Purpose:

\*******************************************************************/

void interpretert::print_variable(const std::string display_name, 
                                  const exprt &expr) const
{
  std::vector<mp_integer> data;
  evaluate(expr, data);

  if (data.size() == 1 || expr.type().id() == ID_array)
  {
    std::cout << display_name <<": ";
    unsigned offset = 0;
    print_values(expr.type(), data, offset);
    std::cout << std::endl;
  }
  else if (expr.id() == ID_symbol && ns.follow(expr.type()).id() == ID_struct)
  {
    std::cout << display_name <<": ";
    unsigned offset = 0;
    print_values(ns.follow(expr.type()), data, offset);
    std::cout << std::endl;
  }
  else
  {
    feature_not_implemented("print");
  }
}

/*******************************************************************\

Function: interpretert::print_values

Inputs:

Outputs:

Purpose:

\*******************************************************************/
void interpretert::feature_not_implemented(const std::string what) const
{
  std::cout << "not implemented for " << what << std::endl;
}

/*******************************************************************\

Function: interpretert::print_values

Inputs:

Outputs:

Purpose:

\*******************************************************************/

void interpretert::print_values(
  const typet &type, 
  const std::vector<mp_integer> values, 
  unsigned &offset) const
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
        print_values(sub_type, values, offset);
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
        if (i != 0) std::cout <<", ";
        print_values(sub_type, values, offset);
      }

      std::cout << "}";
    }
  }
  else if (type.id() == ID_pointer)
  {
    const mp_integer address = values[offset];
    offset++;

    if (address == 0) return;
    
    if( address < memory.size())
    {
      const irep_idt id = memory[integer2long(address)].identifier;
      mp_integer adr = address;
      std::vector<mp_integer> new_values;
      unsigned size = 0;
      while (adr < memory.size() && id == memory[integer2long(adr)].identifier)
      {
        new_values.resize(size + 1);
        new_values[size] = memory[integer2long(adr)].value;
        size++;
        ++adr;
      }

      unsigned new_offset = 0;
      const symbolt &symbol = ns.lookup(id);
      print_values(symbol.type, new_values, new_offset);
    }
  }
  else if(type.id() == ID_symbol)
  {
    print_values(ns.follow(type), values, offset);
  }
  else
  {
    if (offset >= values.size()) return;
    const mp_integer value = values[offset];
    offset++;

    const irep_idt id = type.get(ID_C_c_type);
    if (id == ID_char || id == ID_signed_char || id == ID_unsigned_char)
    {
      int c = int(value.to_long());
      if (isprint(c))
      {
        std::cout << "'" << char(c) << "'";
      }
      else
      {
        std::cout << value;
      }
    }
    else if (id == ID_float || id == ID_double)
    {
      ieee_floatt f;
      f.spec = to_floatbv_type(type);
      f.unpack(value);

      if (f.is_float())
      {
        float x = f.to_float();
        std::cout << x;
      }
      else if (f.is_double())
      {
        double x = f.to_double();
        std::cout << x;
      }
    }
    else
    {
      std::cout << value;
    }
  }
}

/*******************************************************************\

Function: interpretert::list_src

Inputs:

Outputs:

Purpose:

\*******************************************************************/

void interpretert::list_src(unsigned before_lines, unsigned after_lines) const
{
  goto_programt::const_targett cur_PC = PC;
  goto_programt::const_targett start_PC = (function->second).body.instructions.begin();
  goto_programt::const_targett end_PC = (function->second).body.instructions.end();

  unsigned before = 0;
  while (before != before_lines && cur_PC != start_PC)
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
  std::string file = begin_PC->location.is_nil() ? "" : id2string(begin_PC->location.get_file());
  if (file == "")
  {
    // this is the case at the beginning where nothing is executed. find the main
    goto_functionst::function_mapt::const_iterator f_it = find_function("main");
    if (f_it != goto_functions.function_map.end())
    {
      goto_programt::const_targett main_PC = (f_it->second).body.instructions.begin();
      file = main_PC->location.is_nil() ? "" : id2string(main_PC->location.get_file());
    }
  }

  return file;
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

      // remove LF if any
      size_t pos = line.find_last_of('\n');
      if (pos != std::string::npos)
        line.resize(line.size() - 1);

      // remove CR if any
      pos = line.find_last_of('\r');
      if (pos != std::string::npos)
        line.resize(line.size() - 1);

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
      std::string s = first_arg.substr(p, p2 - p);
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
  memory_cellt &cell=memory[address];
  cell.value = 1;
}
