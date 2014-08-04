#ifndef GOTO_INTERPRET_WATCH_H
#define GOTO_INTERPRET_WATCH_H

#include <vector>
#include <stack>
#include <string>

#include <util/arith_tools.h>
#include "goto_functions.h"

/*******************************************************************\

   Class: interpretert_watch

 Purpose: watch management for GOTO interpreter

\*******************************************************************/

class interpreter_watch
{
public:
  interpreter_watch(
    const symbol_tablet &_symbol_table,
    const goto_functionst &_goto_functions):
    symbol_table(_symbol_table),
    ns(_symbol_table),
    goto_functions(_goto_functions)
  {
  }

  bool add(goto_programt::const_targett PC, const std::vector<std::string> variables); 
  bool add(std::string line_no, std::string file, const std::vector<std::string> variables);

  bool remove(goto_programt::const_targett PC); 
  bool remove(goto_programt::const_targett PC, const std::vector<std::string> variables); 
  bool remove(std::string line_no, std::string file, const std::vector<std::string> variables);

  void remove_all();

  void get_watch_variables(goto_programt::const_targett PC, std::vector<std::string> &dest) const;

  void list() const;
protected:
  const symbol_tablet &symbol_table;
  const namespacet ns;
  const goto_functionst &goto_functions;
  
  typedef std::set<std::string> line_watchest;
  typedef hash_map_cont<unsigned, line_watchest> line_listt;
  typedef hash_map_cont<irep_idt, line_listt, irep_id_hash> function_linest;

  function_linest function_lines;

  void add(line_watchest &watches, const std::vector<std::string> variables);

};

#endif
