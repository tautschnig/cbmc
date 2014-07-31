#ifndef GOTO_INTERPRET_BREAKPOINT_H
#define GOTO_INTERPRET_BREAKPOINT_H

#include <set>
#include <stack>
#include <string>

#include <util/arith_tools.h>
#include "goto_functions.h"

/*******************************************************************\

   Class: interpretert_breakpoint

 Purpose: breakpoint management for GOTO interpreter

\*******************************************************************/

class interpreter_breakpoint
{
public:
  interpreter_breakpoint(
    const symbol_tablet &_symbol_table,
    const goto_functionst &_goto_functions):
    symbol_table(_symbol_table),
    ns(_symbol_table),
    goto_functions(_goto_functions)
  {
  }

  bool add_breakpoint(unsigned line_no, goto_programt::const_targett PC);
  bool add_breakpoint(goto_programt::const_targett PC);
  void remove_all_breakpoints();

  bool has_breakpoint_at(goto_programt::const_targett PC);
protected:
  const symbol_tablet &symbol_table;
  const namespacet ns;
  const goto_functionst &goto_functions;
  
  typedef std::set<irep_idt> line_sett;
  typedef hash_map_cont<irep_idt, line_sett, irep_id_hash> module_linest;

  module_linest modules;
};

#endif
