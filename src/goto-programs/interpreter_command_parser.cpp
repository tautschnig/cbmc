/*******************************************************************\

Module: Interpreter command for GOTO Instrument Programs

Author: Siqing Tang, jtang707@gmail.com

\*******************************************************************/

#include <cctype>
#include <cstdio>
#include <algorithm>
#include <iostream>
#include <string>
#include <limits.h>
#include <util/std_types.h>

#include "interpreter_command_parser.h"

#define DEFAULT_LIST_LINES 5

/*******************************************************************\

Function: interpretert_command_parser::help

Inputs: none

Outputs:

Purpose:

\*******************************************************************/

void interpretert_command_parser::help() const
{
  std::cout << "=== General  ===" << std::endl
            << "\thelp - show this help or the usage of a command" << std::endl
            << "\tquit - quit the interpreter" << std::endl
            << std::endl
            << "=== Code Execution ===" << std::endl
            << "\tfunction           - specify an entry function" << std::endl
            << "\tgo                 - run in batch mode" << std::endl
            << "\tmain               - run until the main" << std::endl
            << "\tnext [ENTER]       - run the next line(step over)" << std::endl
            << "\trestart            - restart the goto program" << std::endl
            << "\tstep --into (into) - step into a function" << std::endl
            << "\tstep --over (out)  - step out the current function" << std::endl
            << std::endl
            << "=== Debugging ===" << std::endl
            << "\tbreak     - set/remove breakpoints" << std::endl
            << "\tcallstack - show call stack" << std::endl
            << "\tlist      - list goto-function source code" << std::endl
            << "\tmodify    - modify variable value" << std::endl
            << "\tprint     - print variable value" << std::endl
            << "\tsilent    - turn silent mode on/off" << std::endl
            << "\twatch     - set/remove watch variables" << std::endl
            << "\twhere     - tell the code about to run" << std::endl
            << std::endl
            << "=== File ===" << std::endl
            << "\tsave - save command into a file" << std::endl
            << "\t@    - load commands from a file and run" << std::endl;
}

/*******************************************************************\

Function: interpretert_command_parser::help

Inputs: cmd - command name to help

Outputs: display message on the standard output

Purpose: display help for a specified command

\*******************************************************************/

void interpretert_command_parser::help(std::string cmd) const
{
  if (cmd == "help")
  {
    std::cout << "Command: help | h | ?" << std::endl
              << "Description: show a list of commands and their functions" << std::endl;
  }
  else if (cmd == "quit")
  {
    std::cout << "Command: quit | q" << std::endl
              << "Description: exit the interpreter" << std::endl;
  }
  else if (cmd == "function")
  {
    std::cout << "Command: function|f" << std::endl
              << "Description: set entry function" << std::endl
              << "Usage: function name" << std::endl
              << "       where name is a function name to be run instead of the main" << std::endl;
  }
  else if (cmd == "go")
  {
    std::cout << "Command: go | g" << std::endl
              << "Description: run in batch mode" << std::endl;
  }
  else if (cmd == "main")
  {
    std::cout << "Command: main | m" << std::endl
              << "Description: run until the main" << std::endl;
  }
  else if (cmd == "next")
  {
    std::cout << "Command: next [ENTER]" << std::endl
              << "Description: run the next line(step over)" << std::endl;
  }
  else if (cmd == "restart")
  {
    std::cout << "Command: restart | r" << std::endl
              << "Description: restart the goto program" << std::endl;
  }
  else if (cmd == "step")
  {
    std::cout << "Command: step" << std::endl
              << "Description: step into/out a function" << std::endl
              << "Usage:" << std::endl
              << "   step variable_name    " << std::endl
              << "   step --into [�nto | si] step into a function" << std::endl
              << "   step --over [out  | so]  step out the current function"  << std::endl;
  }
  else if (cmd == "break")
  {
    std::cout << "Command: break | b" << std::endl
              << "Description: set/remove breakpoints" << std::endl
              << "Usage:" << std::endl
              << "   break [--add] ][--line-no=#] [--module=file1] - set breakpoint at a location" << std::endl
              << "   break --remove-all - remove all breakpoints" << std::endl
              << "   break --remove [--line-no=#] [--module=file1] - remove breakpoint at a location" << std::endl;
  }
  else if (cmd == "callstack")
  {
    std::cout << "Command: callstack | cs" << std::endl
              << "Description: show call stack" << std::endl;
  }
  else if (cmd == "list")
  {
    std::cout << "Command: list | l" << std::endl
              << "Description: list goto-function source code" << std::endl
              << "Usage:" << std::endl
              << "   list [--before=#] [--after=#] - list source in a range" << std::endl
              << "   list [--all] - list all source code in the current function" << std::endl;
  }
  else if (cmd == "modify")
  {
    std::cout << "Command: modify | m" << std::endl
              << "Description: modify variable value" << std::endl
              << "Usage:" << std::endl
              << "   modify variable value - modify the variable's value" << std::endl;
  }
  else if (cmd == "print")
  {
    std::cout << "Command: print|p" << std::endl
              << "Description: print varaible value" << std::endl
              << "Usage:" << std::endl
              << "   print variable_name    " << std::endl
              << "   print --all-locals     [pl]  print all local varialbes' value" << std::endl
              << "   print --all-parameters [pp]  print all parameters' value" << std::endl
              << "   print --all-globals    [pg]  print all globals' value" << std::endl
              << "   print --all            [pa]  print locals, parameters and globals" << std::endl;
  }
  else if (cmd == "silent")
  {
    std::cout << "Command: silent" << std::endl
              << "Description: turn silent mode on/off" << std::endl
              << "Usage:" << std::endl
              << "   silent [--on] [son]  turn silent mode on" << std::endl
              << "   silent --off [soff]  turn silent mode off" << std::endl;
  }
  else if (cmd == "watch")
  {
    std::cout << "Command: watch | w" << std::endl
              << "Description: set/remove watch variables" << std::endl
              << "Usage:" << std::endl
              << "   watch [--add] ][--line-no=#] [--module=file1] [v1] [v2] [v3] - add watches" << std::endl
              << "   watch --remove-all - remove all watches" << std::endl
              << "   watch --remove [--line-no=#] [--module=file1] [v1] [v2] [v3] - remove watches" << std::endl;
 
  }
  else if (cmd == "where")
  {
    std::cout << "Command: where" << std::endl
              << "Description: tell the code that is about to run" << std::endl;
  }
  else if (cmd == "save")
  {
    std::cout << "Command: save" << std::endl
              << "Description: save commands a file" << std::endl;
  }
  else if (cmd == "@")
  {
    std::cout << "Command: @ | load" << std::endl
              << "Description: commands from a file and run" << std::endl
              << "Usage:" << std::endl
              << "   @ file   - load commands from a file and run them" << std::endl;
  }
  else
  {
    std::cout << "No help for the Command: " << cmd << "'" << std::endl;
  }
}

/*******************************************************************\

Function: interpretert_command_parser::is_break

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::is_break() const
{
	return cmd == "break";
}

/*******************************************************************\

Function: interpretert_command_parser::is_callstack

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::is_callstack() const
{
	return cmd == "callstack";
}

/*******************************************************************\

Function: interpretert_command_parser::is_function

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::is_function() const
{
	return cmd == "function";
}

/*******************************************************************\

Function: interpretert_command_parser::is_go

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::is_go() const
{
	return cmd == "go";
}

/*******************************************************************\

Function: interpretert_command_parser::is_help

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::is_help() const
{
	return cmd == "help";
}

/*******************************************************************\

Function: interpretert_command_parser::is_load

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::is_load() const
{
	return cmd == "load";
}

/*******************************************************************\

Function: interpretert_command_parser::is_list

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::is_list() const
{
	return cmd == "list";
}

/*******************************************************************\

Function: interpretert_command_parser::is_main

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::is_main() const
{
	return cmd == "main";
}

/*******************************************************************\

Function: interpretert_command_parser::is_modify

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::is_modify() const
{
	return cmd == "modify";
}

/*******************************************************************\

Function: interpretert_command_parser::is_next_line

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::is_next_line() const
{
	return cmd == "next";
}

/*******************************************************************\

Function: interpretert_command_parser::is_print

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::is_print() const
{
	return cmd == "print";
}

/*******************************************************************\

Function: interpretert_command_parser::is_quit

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::is_quit() const
{
	return cmd == "quit";
}

/*******************************************************************\

Function: interpretert_command_parser::is_restart

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::is_restart() const
{
	return cmd == "restart";
}

/*******************************************************************\

Function: interpretert_command_parser::is_save

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::is_save() const
{
	return cmd == "save";
}

/*******************************************************************\

Function: interpretert_command_parser::is_silent

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::is_silent() const
{
	return cmd == "silent";
}

/*******************************************************************\

Function: interpretert_command_parser::is_step_into

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::is_step_into() const
{
	return (cmd == "step" && options.find("into") != options.end()) || (cmd == "");
}

/*******************************************************************\

Function: interpretert_command_parser::is_step_out

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::is_step_out() const
{
	return cmd == "step" && options.find("out") != options.end();
}

/*******************************************************************\

Function: interpretert_command_parser::is_watch

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::is_watch() const
{
	return cmd == "watch";
}

/*******************************************************************\

Function: interpretert_command_parser::is_where

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::is_where() const
{
	return cmd == "where";
}

/*******************************************************************\

Function: interpretert_command_parser::get_parameters

Inputs:

Outputs:

Purpose:

\*******************************************************************/

void interpretert_command_parser::get_parameters(std::vector<std::string> &dest) const
{
	dest.clear();
  for(unsigned i = 0; i < parameters.size(); i++)
	{
		dest.push_back(parameters[i]);
	}
}

/*******************************************************************\

Function: interpretert_command_parser::get_first_parameter

Inputs:

Outputs:

Purpose:

\*******************************************************************/

std::string interpretert_command_parser::get_first_parameter() const
{
  return parameters.size() >= 1 ? parameters[0] : "";
}

/*******************************************************************\

Function: interpretert_command_parser::parse

Inputs:

Outputs:

Purpose:

\*******************************************************************/

void interpretert_command_parser::parse(const char* cmdline)
{
	if (cmdline == NULL)
  {
    cmd = "";
		parameters.clear();
		options.clear();
    return;
  }

  #define SPACE ' '

  parameters.clear();
	options.clear();
  cmd = "";

  line.clear();
  line.append(cmdline);

  line = std::string(cmdline);

  size_t pos;

  // remove LF if any
  pos = line.find_last_of('\n');
  if (pos != std::string::npos)
  {
    line.resize(line.size() - 1);
  }

  // remove CR if any
  pos = line.find_last_of('\r');
  if (pos != std::string::npos)
  {
    line.resize(line.size() - 1);
  }

  pos = line.find_first_not_of(SPACE);
  while (pos != std::string::npos)
  {
    if (cmd.empty() && line[pos] == '@')
    {
      // special calse
      cmd = "@";
      normalise_command(cmd);
      pos = line.find_first_not_of(SPACE, pos + 1);
      continue;
    }

    size_t sp_pos = line.find_first_of(SPACE, pos);
		std::string s;
    if (sp_pos == std::string::npos)
    {
      s = line.substr(pos);
      pos = sp_pos;
    }
    else
    {
      s = line.substr(pos, sp_pos - pos);
      pos = line.find_first_not_of(SPACE, sp_pos + 1);
    }

		if (s.length() >= 2 && s.substr(0, 2) == "--")
		{
			std::string option = s.substr(2);
			if (option.length() > 0)
			{
        if (cmd.empty())
        {
          // any option before a command will be ignored
        }
        else
        {
				  size_t posEq = option.find_first_of("=");
				  std::string value("");
		      if (posEq != std::string::npos)
				  {
					  value = option.substr(posEq + 1);
            option = option.substr(0, posEq);
				  }

				  // convert option into lower case
				  std::transform(option.begin(), option.end(), option.begin(), ::tolower);
				  options[option] = value;
        }
			}
		}
		else 
		{
			if (cmd.empty())
			{
				// the first word, it is a command key word, convert into lower case
				std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        
        // then normalise it
				normalise_command(s);
        cmd = s;
			}
      else
      {
			  parameters.push_back(s);
      }
		}
  }
}

/*******************************************************************\

Function: interpretert_command_parser::normalise_command

Inputs:

Outputs:

Purpose:

\*******************************************************************/

void interpretert_command_parser::normalise_command(std::string &cmd)
{
  if (cmd == "@")
  {
    cmd = "load";
  }
  else if (cmd == "b")
  {
    cmd = "break";
  }
  else if (cmd == "bon")
  {
    cmd = "break";
    options["on"] = "";
  }
  else if (cmd == "boff")
  {
    cmd = "break";
    options["off"] = "";
  }
  else if (cmd == "cs")
  {
    cmd = "callstack";
  }
  else if (cmd == "f")
  {
    cmd = "function";
  }
  //else if (cmd == "go")
  //{
  //  cmd = "go";
  //}
  else if (cmd == "h" || cmd == "?")
  {
    cmd = "help";
  }
  else if (cmd == "l")
  {
    cmd = "list";
  }
  else if (cmd == "la")
  {
    cmd = "list";
    options["all"] = "";
  }
  //else if (cmd == "main")
  //{
  //  cmd = "main";
  //}
  else if (cmd == "m")
  {
    cmd = "modify";
  }
  //else if (cmd == "modify")
  //{
  //  cmd = "modify";
  //}
  else if (cmd == "n")
  {
    cmd = "next";
  }
  else if (cmd == "p") //print
  {
    cmd = "print";
  }
  else if (cmd == "pl") // print --locals
  {
    cmd = "print";
    options["locals"] = "";
  }
  else if (cmd == "pp") // print --parameters
  {
    cmd = "print";
    options["parameters"] = "";
  }
  else if (cmd == "pg") // print --globals
  {
    cmd = "print";
    options["globals"] = "";
  }
  else if (cmd == "plp" || cmd == "ppl") // print --locals --parameters
  {
    cmd = "print";
    options["locals"] = "";
    options["parameters"] = "";
  }
  else if (cmd == "plg" || cmd == "pgl") // print --locals --globals
  {
    cmd = "print";
    options["locals"] = "";
    options["globals"] = "";
  }
  else if (cmd == "ppg" || cmd == "pgp") // print --parameters --globals
  {
    cmd = "print";
    options["parameters"] = "";
    options["globals"] = "";
  }
  else if (cmd == "pa") // (all) print --locals --parameters --globals
  {
    cmd = "print";
    options["locals"] = "";
    options["parameters"] = "";
    options["globals"] = "";
  }
  else if (cmd == "q")
  {
    cmd = "quit";
  }
  else if (cmd == "r")
  {
    cmd = "restart";
  }
  // save
  else if (cmd == "si" || cmd == "in" || cmd == "into")
  {
    cmd = "step";
    options["into"] = "";
  }
  else if (cmd == "so" || cmd == "out" )
  {
    cmd = "step";
    options["out"] = "";
  }
  else if (cmd == "son")
  {
    cmd = "silent";
    options["on"] = "";
  }
  else if (cmd == "soff")
  {
    cmd = "silent";
    options["off"] = "";
  }
  else if (cmd == "w")
  {
    cmd = "watch";
  }
}

/*******************************************************************\

Function: interpretert_command_parser::has_options

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::has_options() const
{
  return !options.empty();
}

/*******************************************************************\

Function: interpretert_command_parser::has_print_locals

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::has_print_locals() const
{
  return options.find("locals") != options.end();
}

/*******************************************************************\

Function: interpretert_command_parser::has_print_parameters

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::has_print_parameters() const
{
  return options.find("parameters") != options.end();
}

/*******************************************************************\

Function: interpretert_command_parser::has_print_globals

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::has_print_globals() const
{
  return options.find("globals") != options.end();
}

/*******************************************************************\

Function: interpretert_command_parser::has_list_all

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::has_list_all() const
{
  return options.find("all") != options.end();
}

/*******************************************************************\

Function: interpretert_command_parser::list_before_lines

Inputs:

Outputs:

Purpose:

\*******************************************************************/

int interpretert_command_parser::list_before_lines() const
{
  if (has_list_all())
  {
    return UINT_MAX;
  }
  else if (options.find("before") != options.end())
  {
    std::string before = options.find("before")->second;
    int value = atoi(before.c_str());
    return (value < 0) ? 0 : value;
  }
  else
  {
    return 0;
  }
}

/*******************************************************************\

Function: interpretert_command_parser::list_after_lines

Inputs:

Outputs:

Purpose:

\*******************************************************************/

int interpretert_command_parser::list_after_lines() const
{
  if (has_list_all())
  {
    return UINT_MAX;
  }
  else if (options.find("after") != options.end())
  {
    std::string after = options.find("after")->second;
    int value = atoi(after.c_str());
    return (value < 0) ? 0 : value;
  }
  else
  {
    return DEFAULT_LIST_LINES;
  }
}

/*******************************************************************\

Function: interpretert_command_parser::has_silent_on

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::has_silent_on() const
{
  return is_silent() && (options.find("off") == options.end());
}

/*******************************************************************\

Function: interpretert_command_parser::has_breakpoint_remove_all

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::has_breakpoint_remove_all() const
{
  return options.find("remove-all") != options.end();
}

/*******************************************************************\

Function: interpretert_command_parser::has_breakpoint_remove

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::has_breakpoint_remove() const
{
  return options.find("remove") != options.end();
}

/*******************************************************************\

Function: interpretert_command_parser::has_breakpoint_add

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::has_breakpoint_add() const
{
  return options.find("add") != options.end();
}

/*******************************************************************\

Function: interpretert_command_parser::has_breakpoint_toggle

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::has_breakpoint_toggle() const
{
  return (options.size() == 0 || 
          options.size() == 1 && options.find("toggle") != options.end()) && 
         parameters.size() == 0;
}

/*******************************************************************\

Function: interpretert_command_parser::has_breakpoint_list

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::has_breakpoint_list() const
{
  return options.find("list") != options.end();
}

/*******************************************************************\

Function: interpretert_command_parser::get_breakpoint_module

Inputs:

Outputs:

Purpose:

\*******************************************************************/

std::string interpretert_command_parser::get_breakpoint_module() const
{
  option_mapt::const_iterator it = options.find("module");
  return it == options.end() ? "" : it->second;
}

/*******************************************************************\

Function: interpretert_command_parser::get_breakpoint_lineno

Inputs:

Outputs:

Purpose:

\*******************************************************************/

std::string interpretert_command_parser::get_breakpoint_lineno() const
{
  option_mapt::const_iterator it = options.find("line-no");
  if (it != options.end())
    return it->second;
  else if (parameters.size() > 0)
    return parameters[0];
  else
    return "";
}

/*******************************************************************\

Function: interpretert_command_parser::option_has_value

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::option_has_value(std::string option) const
{
  option_mapt::const_iterator it = options.find(option);
  return it != options.end() || it->first != "";
}

/*******************************************************************\

Function: interpretert_command_parser::has_save_overwrite

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::has_save_overwrite() const
{
  return options.find("overwrite") != options.end();
}

/*******************************************************************\

Function: interpretert_command_parser::has_watch_remove_all

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::has_watch_remove_all() const
{
  return options.find("remove-all") != options.end();
}

/*******************************************************************\

Function: interpretert_command_parser::has_watch_remove

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::has_watch_remove() const
{
  return options.find("remove") != options.end();
}

/*******************************************************************\

Function: interpretert_command_parser::has_watch_add

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::has_watch_add() const
{
  return options.size() == 0 || options.find("add") != options.end();
}

/*******************************************************************\

Function: interpretert_command_parser::has_watch_list

Inputs:

Outputs:

Purpose:

\*******************************************************************/

bool interpretert_command_parser::has_watch_list() const
{
  return options.find("list") != options.end();
}

/*******************************************************************\

Function: interpretert_command_parser::get_watch_module

Inputs:

Outputs:

Purpose:

\*******************************************************************/

std::string interpretert_command_parser::get_watch_module() const
{
  option_mapt::const_iterator it = options.find("module");
  return it == options.end() ? "" : it->second;
}

/*******************************************************************\

Function: interpretert_command_parser::get_watch_lineno

Inputs:

Outputs:

Purpose:

\*******************************************************************/

std::string interpretert_command_parser::get_watch_lineno() const
{
  option_mapt::const_iterator it = options.find("line-no");
  return it == options.end() ? "" : it->second;
}
