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

void interpretert_command_parser::print_help() const
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

bool interpretert_command_parser::is_break() const
{
	return cmd == "break";
}

bool interpretert_command_parser::is_callstack() const
{
	return cmd == "callstack";
}

bool interpretert_command_parser::is_function() const
{
	return cmd == "function";
}

bool interpretert_command_parser::is_go() const
{
	return cmd == "go";
}

bool interpretert_command_parser::is_help() const
{
	return cmd == "help";
}

bool interpretert_command_parser::is_list() const
{
	return cmd == "list";
}

bool interpretert_command_parser::is_main() const
{
	return cmd == "main";
}

bool interpretert_command_parser::is_modify() const
{
	return cmd == "modify";
}

bool interpretert_command_parser::is_next_line() const
{
	return cmd == "next";
}

bool interpretert_command_parser::is_print() const
{
	return cmd == "print";
}

bool interpretert_command_parser::is_quit() const
{
	return cmd == "quit";
}

bool interpretert_command_parser::is_restart() const
{
	return cmd == "restart";
}

bool interpretert_command_parser::is_silent() const
{
	return cmd == "silent";
}

bool interpretert_command_parser::is_step_into() const
{
	return (cmd == "step" && options.find("into") != options.end()) || (cmd == "");
}

bool interpretert_command_parser::is_step_out() const
{
	return cmd == "step" && options.find("out") != options.end();
}

bool interpretert_command_parser::is_watch() const
{
	return cmd == "watch";
}

bool interpretert_command_parser::is_where() const
{
	return cmd == "where";
}

void interpretert_command_parser::get_parameters(std::vector<std::string> &dest) const
{
	dest.clear();
  for(unsigned i = 0; i < parameters.size(); i++)
	{
		dest.push_back(parameters[i]);
	}
}

std::string interpretert_command_parser::get_first_parameter() const
{
  return parameters.size() >= 1 ? parameters[0] : "";
}

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

  std::string line(cmdline);

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

void interpretert_command_parser::normalise_command(std::string &cmd)
{
  if (cmd == "b")
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

bool interpretert_command_parser::has_options() const
{
  return !options.empty();
}

bool interpretert_command_parser::has_print_locals() const
{
  return options.find("locals") != options.end();
}

bool interpretert_command_parser::has_print_parameters() const
{
  return options.find("parameters") != options.end();
}

bool interpretert_command_parser::has_print_globals() const
{
  return options.find("globals") != options.end();
}

bool interpretert_command_parser::has_list_all() const
{
  return options.find("all") != options.end();
}

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

bool interpretert_command_parser::has_silent_on() const
{
  return is_silent() && (options.find("off") == options.end());
}

bool interpretert_command_parser::has_breakpoint_remove_all() const
{
  return options.find("remove-all") != options.end();
}

bool interpretert_command_parser::has_breakpoint_remove() const
{
  return options.find("remove") != options.end();
}

bool interpretert_command_parser::has_breakpoint_add() const
{
  return (options.size() == 0 || 
          options.find("add") != options.end());
}

bool interpretert_command_parser::has_breakpoint_toggle() const
{
  return (options.size() == 0 || 
          options.find("toggle") != options.end()) && 
         parameters.size() == 0;
}

std::string interpretert_command_parser::get_breakpoint_module() const
{
  option_mapt::const_iterator it = options.find("module");
  return it == options.end() ? "" : it->second;
}

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
