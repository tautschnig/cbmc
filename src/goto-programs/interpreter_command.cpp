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

	std::cout << std::endl << "Command (q to quit; h for help): ";
	if (fgets(command, BUFSIZE - 1, stdin) == NULL)
  {
		tokens.clear();
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

bool interpretert_command::is_quit() const
{
	return !tokens.empty() && (
		tokens.front() == "quit" ||
		tokens.front() == "q");
}

bool interpretert_command::is_help() const
{
	return !tokens.empty() && (
		tokens.front() == "help" ||
		tokens.front() == "h"    ||
		tokens.front() == "?");
}

bool interpretert_command::is_restart() const
{
	return !tokens.empty() && (
		tokens.front() == "restart" ||
		tokens.front() == "r");
}

bool interpretert_command::is_run_until_main() const
{
	return !tokens.empty() && (
		tokens.front() == "main" ||
		tokens.front() == "m");
}

bool interpretert_command::is_next_line() const
{
	return tokens.empty() ||
		tokens.front() == "next" ||
		tokens.front() == "n";
}

bool interpretert_command::is_step_into() const
{
	return !tokens.empty() && (
		(tokens.size() == 1 && tokens.front() == "si") ||
		(tokens.size() == 2 && 
		 tokens.front() == "step" && 
		 tokens[1] == "into"));
}

bool interpretert_command::is_step_over() const
{
	return !tokens.empty() && (
		(tokens.size() == 1 && tokens.front() == "so") ||
		(tokens.size() == 2 && 
		 tokens.front() == "step" && 
		 tokens[1] == "over"));
}

bool interpretert_command::is_print() const
{
	return !tokens.empty() &&
		(tokens.front() == "print" ||
		 tokens.front() == "p");
}

bool interpretert_command::is_list() const
{
	return !tokens.empty() &&
		(tokens.front() == "list" ||
		 tokens.front() == "l");
}

void interpretert_command::get_parameters(std::vector<std::string> &dest) const
{
	dest.clear();

	unsigned skip = 1;

	// more logics here. for now just skip 1
	// for example
	// if (is_print())
	//    skip = 1
	// else if (...)
	//
	if (skip >= tokens.size()) return;

	dest.resize(tokens.size() - skip);
	for(unsigned i = skip; i < tokens.size(); i++)
	{
		dest.push_back(tokens[i]);
	}
}

void interpretert_command::get_options(std::vector<std::string> &dest) const
{
	dest.clear();
	for(unsigned i = 0; i < options.size(); i++)
	{
		//dest.push_back(options[i]);
	}
}

void interpretert_command::parse(const char* cmdline)
{
  #define SPACE ' '

  tokens.clear();
	options.clear();

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
		else 
		{
			if (tokens.empty())
			{
				// the first word, it is a command key word, convert into lower case
				std::transform(s.begin(), s.end(), s.begin(), ::tolower);
				s = normalise_command(s);
			}
			tokens.push_back(s);
		}
  }
}


std::string interpretert_command::normalise_command(const std::string cmd)
{
	std::string result(cmd);

	return result;
}
