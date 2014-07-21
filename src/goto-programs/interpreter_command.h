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

	bool is_break() const;
	bool is_callstack() const;
	bool is_help() const;
	bool is_list() const;
	bool is_run_until_main() const;
	bool is_next_line() const;
	bool is_print() const;
	bool is_quit() const;
	bool is_restart() const;
	bool is_step_into() const;
	bool is_step_over() const;
	bool is_silent() const;
	bool is_watch() const;

	bool has_options() const;

  //options for print
	bool has_print_locals() const;
	bool has_print_parameters() const;
	bool has_print_globals() const;

  void get_parameters(std::vector<std::string> &dest) const;
protected:
  std::string cmd;
	std::vector<std::string> parameters;

	// --blah or --blah=blah is an option.
	// all options are stored in options map
	std::unordered_map<std::string, std::string> options;

	void parse(const char* cmdline);

	void normalise_command(std::string &cmd);
};

#endif /* GOTO_INTERPRET_COMMAND_H */
