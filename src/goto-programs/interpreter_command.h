/*******************************************************************\

   Class: interpretert_command

 Purpose: interpreter command parser GOTO interpreter

\*******************************************************************/

#ifndef GOTO_INTERPRET_COMMAND_H
#define GOTO_INTERPRET_COMMAND_H

#include <vector>
#include <string>
#include <unordered_map>

class interpretert_command
{

public:
  interpretert_command()
	{
  }
  
  void ask();

	void print_help() const;

	bool is_quit() const;
	bool is_help() const;
	bool is_restart() const;
	bool is_run_until_main() const;
	bool is_next_line() const;
	bool is_step_into() const;
	bool is_step_over() const;
	bool is_print() const;
	bool is_list() const;

  void get_parameters(std::vector<std::string> &dest) const;
  void get_options(std::vector<std::string> &dest) const;
protected:
	std::vector<std::string> tokens; //all but options (see below)

	// --blah or --blah=blah is an option.
	// all options are stored in options vector
	std::unordered_map<std::string, std::string> options;

	void parse(const char* cmdline);

	std::string normalise_command(const std::string cmd);
};

#endif /* GOTO_INTERPRET_COMMAND_H */
