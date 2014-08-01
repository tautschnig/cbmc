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

#include "interpreter_breakpoint.h"

bool interpreter_breakpoint::add_breakpoint(std::string line_no, std::string module)
{
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
        id2string(PC->location.get_file()) == module)
    {
      while (PC != goto_function.body.instructions.end())
      {
        std::string line = id2string(PC->location.get_line());
        if (line == line_no) add_breakpoint(PC, false);
        PC++;
      }
    }
  }

  return false;
}

bool interpreter_breakpoint::add_breakpoint(
  goto_programt::const_targett PC, bool toggle)
{
  unsigned location_number = PC->location_number;
  function_linest::iterator f_it = function_lines.find(PC->function);

  if (f_it == function_lines.end())
  {
    function_lines[PC->function] = line_listt();
    line_listt &lines = function_lines[PC->function];
    lines.push_back(location_number);
    return true;
  }
  else
  {
    line_listt &lines = f_it->second;
    for(unsigned i=0; i < lines.size(); i++)
    {
      if (lines[i] == location_number)
      {
        if (toggle) lines.erase(lines.begin() + i);
        return false;
      }
    }

    lines.push_back(location_number);
    return true;
  }

  return false;
}

bool interpreter_breakpoint::remove_breakpoint(goto_programt::const_targett PC)
{
  unsigned location_number = PC->location_number;
  function_linest::iterator f_it = function_lines.find(PC->function);

  if (f_it != function_lines.end())
  {
    line_listt &lines = f_it->second;
    for(unsigned i=0; i < lines.size(); i++)
    {
      if (lines[i] == location_number)
      {
        lines.erase(lines.begin() + i);
        return true;
      }
    }
  }

  return false;
}

bool interpreter_breakpoint::remove_breakpoint(std::string line_no, std::string module)
{
  bool removed = false;
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
        id2string(PC->location.get_file()) == module)
    {
      while (PC != goto_function.body.instructions.end())
      {
        std::string line = id2string(PC->location.get_line());
        if (line == line_no) 
        {
          if (remove_breakpoint(PC)) removed = true;
        }
        PC++;
      }
    }
  }

  return removed;
}

void interpreter_breakpoint::remove_all()
{
  function_lines.clear();
}

bool interpreter_breakpoint::has_breakpoint_at(goto_programt::const_targett PC) const
{
  function_linest::const_iterator f_it = function_lines.find(PC->function);
  return f_it != function_lines.end() && 
         has_breakpoint_at(f_it->second, PC->location_number);
}

bool interpreter_breakpoint::has_breakpoint_at(
  const line_listt &lines, 
  unsigned location_number) const
{
  for(unsigned i=0; i < lines.size(); i++)
  {
    if (lines[i] == location_number)
      return true;
  }

  return false;
}

void interpreter_breakpoint::list() const
{
  for(function_linest::const_iterator 
      it = function_lines.begin();
      it != function_lines.end();
      it++)
  {
    const line_listt &lines = it->second;
    if (lines.size() == 0) continue;

    goto_functionst::function_mapt::const_iterator f_it = 
      goto_functions.function_map.find(it->first);

    if (f_it == goto_functions.function_map.end()) continue;

    const goto_functionst::goto_functiont &function = f_it->second;

    std::string function_name = id2string(f_it->first);
    if (function_name.find("c::") == 0)
    {
      function_name = function_name.substr(3);
    }

    std::cout << "Breakpoint(s) set in function '" << function_name << "' " << std::endl;

    for(goto_programt::const_targett PC = function.body.instructions.begin();
        PC != function.body.instructions.end();
        PC++)
    {
      if (has_breakpoint_at(lines, PC->location_number))
      {
        f_it->second.body.output_instruction(ns, f_it->first, std::cout, PC);
      }
    }

    if (has_breakpoint_at(lines, function.body.instructions.end()->location_number))
    {
      std::cout << "End of function '"
                << f_it->first << "'" << std::endl;
    }
  }
}