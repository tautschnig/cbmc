/*******************************************************************\

Module: Breakpoint for Goto Interpreter

Author: Siqing Tang, jtang707@gmail.com

\*******************************************************************/

#include <iostream>
#include <util/std_types.h>
#include "interpreter_watch.h"

void interpreter_watch::add(line_watchest &watches, const std::vector<std::string> variables)
{
  for(unsigned i = 0; i < variables.size(); i++)
  {
    watches.insert(variables[i]);
    std::cout << variables[i] << " added to watch list" << std::endl;
  }
}

bool interpreter_watch::add(goto_programt::const_targett PC, const std::vector<std::string> variables)
{
  if (variables.size() == 0) return false;

  unsigned location_number = PC->location_number;
  function_linest::iterator f_it = function_lines.find(PC->function);

  if (f_it == function_lines.end())
  {
    function_lines[PC->function] = line_listt();

    line_listt &lines = function_lines[PC->function];
    lines[location_number] = line_watchest();

    line_watchest &watches = lines[location_number];
    add(watches, variables);
  }
  else
  {
    line_listt &lines = f_it->second;
    line_listt::iterator it = lines.find(location_number);
    if (it == lines.end())
    {
      lines[location_number] = line_watchest();
      line_watchest &watches = lines[location_number];
      add(watches, variables);
    }
    else
    {
      line_watchest &watches = it->second;
      add(watches, variables);
    }
  }

  return true; 
}

bool interpreter_watch::add(
  std::string line_no, std::string file, 
  const std::vector<std::string> variables)
{
  if (variables.size() == 0) return false;

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
        id2string(PC->location.get_file()) == file)
    {
      while (PC != goto_function.body.instructions.end())
      {
        std::string line = id2string(PC->location.get_line());
        if (line == line_no) add(PC, variables);
        PC++;
      }
    }
  }

  return true;
}

bool interpreter_watch::remove(goto_programt::const_targett PC)
{
  function_linest::iterator f_it = function_lines.find(PC->function);

  if (f_it != function_lines.end())
  {
    line_listt &lines = f_it->second;
    lines.erase(PC->location_number);
    std::cout << " watches at " << PC->location_number 
              << " removed" << std::endl;
  }

  return false;
}

bool interpreter_watch::remove(
  goto_programt::const_targett PC, 
  const std::vector<std::string> variables)
{
  if (variables.size() == 0) return false;

  function_linest::iterator f_it = function_lines.find(PC->function);

  if (f_it != function_lines.end())
  {
    line_listt &lines = f_it->second;
    line_listt::iterator it = lines.find(PC->location_number);
    if (it != lines.end())
    {
      std::set<std::string> &watches = it->second;
      for(unsigned i = 0; i < variables.size(); i++)
      {
        watches.erase(variables[i]);
        std::cout << variables[i] << " at "
                  << PC->location_number << " removed" << std::endl;
      }
    }
  }

  return true;
}

bool interpreter_watch::remove(
  std::string line_no, 
  std::string file, 
  const std::vector<std::string> variables)
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
        id2string(PC->location.get_file()) == file)
    {
      while (PC != goto_function.body.instructions.end())
      {
        std::string line = id2string(PC->location.get_line());
        if (line == line_no) 
        {
          if (remove(PC)) removed = true;
        }
        PC++;
      }
    }
  }

  return removed;
}

void interpreter_watch::remove_all()
{
  function_lines.clear();
}

void interpreter_watch::list() const
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

    bool first = true;

    for(goto_programt::const_targett PC = function.body.instructions.begin();
        PC != function.body.instructions.end();
        PC++)
    {
      line_listt::const_iterator watches_it = lines.find(PC->location_number);
      if (watches_it != lines.end())
      {
        const std::set<std::string> &watches = watches_it->second;
        if (!watches.empty())
        {
          if (first)
          {
            first = false;
            std::cout << "Watches in function '" << function_name << "' " << std::endl;
          }

          f_it->second.body.output_instruction(ns, f_it->first, std::cout, PC);
          std::cout << "Watch(es): ";
          for (std::set<std::string>::const_iterator watch_it = watches.begin(); 
               watch_it != watches.end(); 
               watch_it++)
          {
            if (watch_it != watches.begin()) std::cout << ", ";
            std::cout << *watch_it;
          }
          std::cout << std::endl;
        }
      }
    }
  }
}

void interpreter_watch::get_watch_variables(
  goto_programt::const_targett PC, 
  std::vector<std::string> &dest) const
{
  function_linest::const_iterator f_it = function_lines.find(PC->function);
  if (f_it == function_lines.end()) return;
  const line_listt &lines = f_it->second;
  line_listt::const_iterator &watches_it = lines.find(PC->location_number);
  if (watches_it != lines.end())
  {
    const line_watchest &watches = watches_it->second;
    for (std::set<std::string>::const_iterator watch_it = watches.begin(); 
         watch_it != watches.end(); 
         watch_it++)
    {
      dest.push_back(*watch_it);
    }
  }
}
