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

bool interpreter_breakpoint::add_breakpoint(unsigned line_no, std::string module)
{
  return false;
}

bool interpreter_breakpoint::add_breakpoint(goto_programt::const_targett PC)
{
  unsigned location_number = PC->location_number;
  function_linest::iterator f_it = function_lines.find(PC->function);

  if (f_it == function_lines.end())
  {
    function_lines[PC->function] = line_listt();
    line_listt *lines = &(function_lines[PC->function]);
    lines->push_back(location_number);
    return true;
  }
  else
  {
    line_listt *lines = &(f_it->second);
    for(unsigned i=0; i < lines->size(); i++)
    {
      if ((*lines)[i] == location_number)
      {
        lines->erase(lines->begin() + i);
        return true;
      }
    }

    lines->push_back(location_number);
    return true;
  }

  return false;
}

void interpreter_breakpoint::remove_all_breakpoints()
{
  function_lines.clear();
}

bool interpreter_breakpoint::has_breakpoint_at(goto_programt::const_targett PC)
{
  unsigned location_number = PC->location_number;
  function_linest::const_iterator f_it = function_lines.find(PC->function);

  if (f_it != function_lines.end())
  {
    const line_listt *lines = &(f_it->second);
    for(unsigned i=0; i < lines->size(); i++)
    {
      if ((*lines)[i] == location_number)
        return true;
    }
  }

  return false;
}
