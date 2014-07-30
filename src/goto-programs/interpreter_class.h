/*******************************************************************\

Module: Interpreter for GOTO Programs

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

/// \file
/// Interpreter for GOTO Programs

#ifndef CPROVER_GOTO_PROGRAMS_INTERPRETER_CLASS_H
#define CPROVER_GOTO_PROGRAMS_INTERPRETER_CLASS_H

#include "interpreter_command_parser.h"
#include "interpreter_breakpoint.h"

#include <stack>

#include <util/arith_tools.h>

#include "goto_functions.h"

class interpretert
{
public:
  interpretert(
    const symbol_tablet &_symbol_table,
    const goto_functionst &_goto_functions):
    symbol_table(_symbol_table),
    ns(_symbol_table),
    goto_functions(_goto_functions)
  {
    entry_function = "";
    silent = false;
    batch_mode = false;
    break_point = new interpreter_breakpoint(_symbol_table, _goto_functions);
  }

  ~interpretert()
  {
    delete break_point;
  }

  void operator()();

protected:
  const symbol_tablet &symbol_table;
  const namespacet ns;
  const goto_functionst &goto_functions;

  typedef std::unordered_map<irep_idt, unsigned, irep_id_hash> memory_mapt;
  memory_mapt memory_map;

  class memory_cellt
  {
  public:
    irep_idt identifier;
    unsigned offset;
    mp_integer value;
  };

  typedef std::vector<memory_cellt> memoryt;
  memoryt memory;

  std::size_t stack_pointer;

  void build_memory_map();
  void build_memory_map(const symbolt &symbol);
  unsigned get_size(const typet &type) const;
  void step();

  void show_help();
  void fix_argc();
  
  void execute_assert();
  void execute_assume();
  void execute_assign();
  void execute_goto();
  void execute_function_call();
  void execute_other();
  void execute_printf() const;
  void execute_decl();

  bool is_string_constant( const exprt &expr) const;

  void print_arg(const std::string str_format, const exprt &expr) const;

  void assign(
    mp_integer address,
    const std::vector<mp_integer> &rhs);

  void read(
    mp_integer address,
    std::vector<mp_integer> &dest) const;

  std::string read_string(const std::vector<mp_integer> from) const;

	interpretert_command_parser cmd;
  interpreter_breakpoint *break_point;

  void command();

  class stack_framet
  {
  public:
    goto_programt::const_targett return_PC;
    goto_functionst::function_mapt::const_iterator return_function;
    mp_integer return_value_address;
    memory_mapt local_map;
    unsigned old_stack_pointer;
  };

  typedef std::stack<stack_framet> call_stackt;
  call_stackt call_stack;

  goto_functionst::function_mapt::const_iterator function, initial_function, next_stop_function;
  goto_programt::const_targett PC, next_PC, next_stop_PC;
  
  bool done;
  bool completed;
  bool restart;
  bool run_upto_main;
  bool main_called;
  bool next_line;
  bool step_out;
  bool next_stop_PC_set;
	bool running;
  bool silent;
  bool batch_mode;
  std::string entry_function;

  const symbolt null_symbol;

  void print_local_variables(bool include_args, bool include_real_locals) const;
  void print_global_varialbes() const;
  void remove_global_varialbe_prefix(std::string &name) const;
  const symbolt &get_variable_symbol(const std::string variable) const;
  //const symbolt &get_variable_symbol(const std::string variable);

  void print_variable(const std::string variable) const;
  //void print_variable_value(const std::string variable, const typet &_type, const irep_idt &id) const;
  void print_variable(const std::string display_name, const symbolt &symbol) const;
  void print_struct(const typet &type, const std::vector<mp_integer> values, unsigned &offset) const;

  bool is_internal_global_varialbe(const std::string name) const;

  void reset_next_PC();
  void show_function_start_msg() const;
	void show_require_running_msg() const;
	void print() const;
  void list_src(int before_lines, int after_lines) const;
  void show_callstack() const;
  void set_entry_function(std::string);
  void modify_variable();
  void modify_variable(const symbolt &symbol, const exprt &expr);

  // helper
  goto_functionst::function_mapt::const_iterator find_function(std::string function_name) const;

  bool evaluate_boolean(const exprt &expr) const
  {
    std::vector<mp_integer> v;
    evaluate(expr, v);
    if(v.size()!=1)
      throw "invalid boolean value";
    return v.front()!=0;
  }

  void evaluate(
    const exprt &expr,
    std::vector<mp_integer> &dest) const;

  mp_integer evaluate_address(const exprt &expr) const;

  void show_state() const;
  void show_state(bool force) const;
};

#endif // CPROVER_GOTO_PROGRAMS_INTERPRETER_CLASS_H
