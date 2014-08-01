/*******************************************************************\

Module: Breakpoint for Goto Interpreter

Author: Siqing Tang, jtang707@gmail.com

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

#include "interpreter_watch.h"

bool interpreter_watch::add(goto_programt::const_targett PC, std::vector<std::string> variables)
{
  return false;
}

bool interpreter_watch::add(std::string line_no, std::string module, std::vector<std::string> variables)
{
  return false;
}

bool interpreter_watch::remove(goto_programt::const_targett PC)
{
  return false;
}

bool interpreter_watch::remove(goto_programt::const_targett PC, std::vector<std::string> variables)
{
  return false;
}

bool interpreter_watch::remove(std::string line_no, std::string module, std::vector<std::string> variables)
{
  return false;
}

void interpreter_watch::remove_all()
{
}

void interpreter_watch::get_watch_variables(goto_programt::const_targett PC, std::vector<mp_integer> &dest) const
{
}

void interpreter_watch::list() const
{
}
