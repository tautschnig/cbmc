/*******************************************************************\

   Class: interpretert_command

 Purpose: interpreter command parser GOTO interpreter

\*******************************************************************/

#ifndef GOTO_INTERPRETER_COMMAND_PARSER_H
#define GOTO_INTERPRETER_COMMAND_PARSER_H

#include <vector>
#include <util/hash_cont.h>

class interpretert_command_parser
{
public:
  interpretert_command_parser()
	{
  }
  
	void parse(const char* cmdline);

	void print_help() const;

	bool is_break() const;
	bool is_callstack() const;
	bool is_function() const;
  bool is_go() const;
	bool is_help() const;
	bool is_list() const;
	bool is_main() const; //run up to the main (could be another function specified by the 'function' command
  bool is_modify() const;
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
	bool has_silent_on() const;

  bool has_breakpoint_remove_all() const;
	bool has_breakpoint_remove() const;
	bool has_breakpoint_add() const;
	bool has_breakpoint_toggle() const;
	bool has_breakpoint_list() const;
	std::string get_breakpoint_module() const;
	std::string get_breakpoint_lineno() const;

  void get_parameters(std::vector<std::string> &dest) const;
  std::string get_first_parameter() const;
  bool option_has_value(std::string option) const;
protected:
  std::string cmd;
	std::vector<std::string> parameters;

  typedef hash_map_cont<std::string, std::string, string_hash> option_mapt;

	// --blah or --blah=blah is an option.
	// all options are stored in options map
	option_mapt options;

	void normalise_command(std::string &cmd);
};

#endif /* GOTO_INTERPRETER_COMMAND_PARSER_H */
