/*******************************************************************\

Module: Interpreter for GOTO Programs

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <string>
#include <cctype>
#include <cstdio>
#include <algorithm>
#include <iostream>
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
  std::cout << id2string(PC->location.get_line()) << std::endl;

  return false;
}

void interpreter_breakpoint::remove_all_breakpoints()
{
}

bool interpreter_breakpoint::has_breakpoint_at(goto_programt::const_targett PC)
{
  return false;
}
