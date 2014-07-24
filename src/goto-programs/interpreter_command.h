/*******************************************************************\

   Class: interpretert_command

 Purpose: interpreter command parser GOTO interpreter

\*******************************************************************/

#ifndef GOTO_INTERPRET_COMMAND_H
#define GOTO_INTERPRET_COMMAND_H

//#include <vector>
//#include <string>
#include <vector>
#include <util/hash_cont.h>

class interpretert_command
{

public:
  interpretert_command()
	{
  }
  
	void parse(const char* cmdline);

	void print_help() const;

	bool is_break() const;
	bool is_callstack() const;
	bool is_help() const;
	bool is_list() const;
	bool is_run_until_main() const;
	bool is_next_line() const; //step over = ENTER
	bool is_print() const;
	bool is_quit() const;
	bool is_restart() const;
	bool is_step_into() const;
	bool is_step_out() const;
	bool is_silent() const;
	bool is_watch() const;
	bool is_where() const;

	bool has_options() const;

  //options for print
	bool has_print_locals() const;
	bool has_print_parameters() const;
	bool has_print_globals() const;
  bool has_list_all() const;
  int list_before_lines() const;
  int list_after_lines() const;

  void get_parameters(std::vector<std::string> &dest) const;
protected:
  std::string cmd;
	std::vector<std::string> parameters;

  typedef hash_map_cont<std::string, std::string, string_hash> option_mapt;

	// --blah or --blah=blah is an option.
	// all options are stored in options map
	option_mapt options;

	void normalise_command(std::string &cmd);
};

#endif /* GOTO_INTERPRET_COMMAND_H */
