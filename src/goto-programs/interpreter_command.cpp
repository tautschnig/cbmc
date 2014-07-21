/*******************************************************************\

Module: Interpreter command for GOTO Instrument Programs

Author: Siqing Tang, jtang707@gmail.com

\*******************************************************************/

#include <cctype>
//#include <cstdio>
#include <iostream>
#include <algorithm>
#include <string>
#include <unordered_map>
//#include <utility>
#include <util/std_types.h>

#include "interpreter_command.h"

void interpretert_command::ask()
{
	#define BUFSIZE 100

	char command[BUFSIZE];  

	//std::cout << std::endl << "Command (q to quit; h for help): ";
	
	std::cout << std::endl << ":";
	if (fgets(command, BUFSIZE - 1, stdin) == NULL)
  {
    cmd = "";
		parameters.clear();
		options.clear();
  }
	else
	{
		parse(command);
	}
}

void interpretert_command::print_help() const
{
  std::cout << "\tq - quit" << std::endl
            << "\th - help" << std::endl
            << "\tr - restart" << std::endl;
  
  //if (completed) return;

  std::cout << "\tm - run until the main" << std::endl
            << "\tn - next line [ENTER]" << std::endl
            << "\tsi - step into" << std::endl
            << "\tso - step out" << std::endl;
}

bool interpretert_command::is_break() const
{
	return cmd == "break";
}

bool interpretert_command::is_callstack() const
{
	return cmd == "callstack";
}

bool interpretert_command::is_help() const
{
	return cmd == "help";
}

bool interpretert_command::is_list() const
{
	return cmd == "list";
}

bool interpretert_command::is_run_until_main() const
{
	return cmd == "main";
}

bool interpretert_command::is_next_line() const
{
	return cmd == "next" || cmd == "";
}

bool interpretert_command::is_print() const
{
	return cmd == "print";
}

bool interpretert_command::is_quit() const
{
	return cmd == "quit";
}

bool interpretert_command::is_restart() const
{
	return cmd == "restart";
}

bool interpretert_command::is_silent() const
{
	return cmd == "silent";
}

bool interpretert_command::is_step_into() const
{
	return cmd == "step" && options.find("into") != options.end();
}

bool interpretert_command::is_step_over() const
{
	return cmd == "step" && options.find("over") != options.end();
}

bool interpretert_command::is_watch() const
{
	return cmd == "watch";
}

void interpretert_command::get_parameters(std::vector<std::string> &dest) const
{
	dest.clear();
  for(unsigned i = 0; i < parameters.size(); i++)
	{
		dest.push_back(parameters[i]);
	}
}

void interpretert_command::parse(const char* cmdline)
{
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

void interpretert_command::normalise_command(std::string &cmd)
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
  else if (cmd == "h" || cmd == "?")
  {
    cmd = "help";
  }
  else if (cmd == "l")
  {
    cmd = "list";
  }
  else if (cmd == "m")
  {
    cmd = "main";
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
  else if (cmd == "si")
  {
    cmd = "step";
    options["into"] = "";
  }
  else if (cmd == "so")
  {
    cmd = "step";
    options["over"] = "";
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

bool interpretert_command::has_options() const
{
  return !options.empty();
}

bool interpretert_command::has_print_locals() const
{
  return options.find("locals") != options.end();
}

bool interpretert_command::has_print_parameters() const
{
  return options.find("parameters") != options.end();
}

bool interpretert_command::has_print_globals() const
{
  return options.find("globals") != options.end();
}

