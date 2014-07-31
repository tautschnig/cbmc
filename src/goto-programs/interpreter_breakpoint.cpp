/*******************************************************************\

Module: Interpreter for GOTO Programs

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <string>
#include <cctype>
#include <cstdio>
#include <algorithm>
#include <iostream>
#include <map>
#include <string>
#include <limits.h>
#include <util/std_types.h>

#include "interpreter_breakpoint.h"

bool interpreter_breakpoint::add_breakpoint(unsigned line_no, goto_programt::const_targett PC)
{
  if (PC->location.is_nil()) return false;

  std::cout << id2string(PC->location.get_line()) << std::endl;

  return false;
}

bool interpreter_breakpoint::add_breakpoint(goto_programt::const_targett PC)
{
  if (PC->location.is_nil()) return false;
  

  const irep_idt &file = PC->location.get_file();

  line_sett *lines;

  module_linest::iterator m_it = modules.find(file);

  if (m_it == modules.end())
  {
    modules[file] = line_sett();
    lines = &modules[file];
  }
  else
  {
    lines = &m_it->second;
  }

  lines->emplace(PC->location.get_line());

  std::cout << id2string(PC->location.get_line()) << std::endl;
  std::cout << "added breakpoint at " << PC->location.get_line() << std::endl;

  return false;
}

void interpreter_breakpoint::remove_all_breakpoints()
{
  modules.clear();
}

bool interpreter_breakpoint::has_breakpoint_at(goto_programt::const_targett PC)
{
  if (PC->location.is_nil()) return false;

  const irep_idt &file = PC->location.get_file();
  module_linest::const_iterator m_it = modules.find(file);

  if (m_it != modules.end())
  {
    const line_sett *lines = &m_it->second;
    if ((lines->find(PC->location.get_line())) != lines->end())
    {
      //const std::string s = id2string(PC->location.get_line());
      //std::cout << "has breakpoint at " << PC->location.get_line() << std::endl;

      return true;
    }
  }

  return false;
}
