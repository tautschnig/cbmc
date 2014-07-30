/*******************************************************************\

Module: Interpreter for GOTO Programs

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <ctype.h>
#include <stdio.h>
#include <cctype>
#include <cstdio>
#include <iostream>

#include "interpreter_breakpoint.h"

bool interpretert_breakpoint::add_breakpoint(unsigned line_no, goto_programt::const_targett PC)
{
  return false;
}

bool interpretert_breakpoint::add_breakpoint(goto_programt::const_targett PC)
{
  return false;
}

void interpretert_breakpoint::remove_all_breakpoints(goto_programt::const_targett PC)
{
}

/*******************************************************************\

Function: interpretert_breakpoint::operator()

Inputs: PC (goto_programt::const_targett)

Outputs: true/false

Purpose: check if there is a breakpoint at PC specified.

\*******************************************************************/

bool interpretert_breakpoint::has_breakpoint_at(goto_programt::const_targett PC)
{
  return false;
}
