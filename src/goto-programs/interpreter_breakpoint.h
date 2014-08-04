#ifndef GOTO_INTERPRET_BREAKPOINT_H
#define GOTO_INTERPRET_BREAKPOINT_H

#include <vector>
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

  bool add(std::string line_no, std::string file);

  bool add(goto_programt::const_targett PC) 
  {
    return add(PC, false);
  };

  bool toggle(goto_programt::const_targett PC) 
  {
    return add(PC, true);
  };

  bool remove(goto_programt::const_targett PC); 
  bool remove(std::string line_no, std::string file);
  void remove_all();

  bool has_breakpoint_at(goto_programt::const_targett PC) const;
  void list() const;
protected:
  bool add(goto_programt::const_targett PC, bool toggle);
  const symbol_tablet &symbol_table;
  const namespacet ns;
  const goto_functionst &goto_functions;
  
  typedef std::vector<unsigned> line_listt;
  typedef hash_map_cont<irep_idt, line_listt, irep_id_hash> function_linest;

  function_linest function_lines;
  bool has_breakpoint_at(const line_listt &lines, unsigned location_number) const;
};

#endif
