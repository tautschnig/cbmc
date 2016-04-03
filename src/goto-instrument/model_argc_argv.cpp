/*******************************************************************\

Module: Initialize command line arguments

Author: Michael Tautschnig

Date: April 2016

\*******************************************************************/

#include <sstream>

#include <util/message.h>
#include <util/namespace.h>
#include <util/config.h>
#include <util/replace_symbol.h>
#include <util/symbol_table.h>
#include <util/prefix.h>

#include <ansi-c/ansi_c_language.h>

#include <goto-programs/goto_convert.h>
#include <goto-programs/goto_functions.h>
#include <goto-programs/remove_skip.h>

#include "model_argc_argv.h"

/*******************************************************************\

Function: model_argc_argv

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool model_argc_argv(
  symbol_tablet &symbol_table,
  goto_functionst &goto_functions,
  unsigned max_argc,
  message_handlert &message_handler)
{
  messaget message(message_handler);
  const namespacet ns(symbol_table);

  goto_functionst::function_mapt::iterator init_entry=
    goto_functions.function_map.find("__CPROVER_initialize");
  if(init_entry==goto_functions.function_map.end() ||
     !init_entry->second.body_available())
  {
    message.error() << "Linking not done, missing __CPROVER_initialize"
                    << messaget::eom;
    return true;
  }

  goto_programt &init=init_entry->second.body;
  goto_programt::targett init_end=init.instructions.end();
  --init_end;
  assert(init_end->is_end_function());
  assert(init_end!=init.instructions.begin());
  --init_end;

  const symbolt &main_symbol=
    ns.lookup(config.main.empty()?ID_main:config.main);

  const code_typet::parameterst &parameters=
    to_code_type(main_symbol.type).parameters();
  if(parameters.size()!=2 &&
     parameters.size()!=3)
  {
    message.error() << "main expected to take 2 or 3 arguments"
                    << messaget::eom;
    return true;
  }

  // set the size of ARGV storage to 4096, which matches the minium
  // guaranteed by POSIX (_POSIX_ARG_MAX):
  // http://pubs.opengroup.org/onlinepubs/009695399/basedefs/limits.h.html
  std::ostringstream oss;
  oss <<
    "int ARGC;\n\
     char *ARGV[1];\n\
     void __CPROVER_initialize()\n\
     {\n\
	     unsigned next=0u;\n\
       __CPROVER_assume(ARGC>=1);\n\
       __CPROVER_assume(ARGC<=" << max_argc << ");\n\
       __CPROVER_thread_local static char arg_string[4096];\n\
       for(unsigned i=0u; i<ARGC && i<" << max_argc << "; ++i)\n\
       {\n\
         unsigned len;\n\
         __CPROVER_assume(len<4096);\n\
         __CPROVER_assume(next+len<4096);\n\
         __CPROVER_assume(arg_string[next+len]==0);\n\
         ARGV[i]=&(arg_string[next]);\n\
         next+=len+1;\n\
       }\n\
     }";
  std::istringstream iss(oss.str());

  ansi_c_languaget ansi_c_language;
  ansi_c_language.set_message_handler(message_handler);
  configt::ansi_ct::preprocessort pp=config.ansi_c.preprocessor;
  config.ansi_c.preprocessor=configt::ansi_ct::preprocessort::NO_PP;
  ansi_c_language.parse(iss, "");
  config.ansi_c.preprocessor=pp;

  symbol_tablet tmp_symbol_table;
  ansi_c_language.typecheck(tmp_symbol_table, "<built-in-library>");

  goto_programt tmp;
  exprt value=nil_exprt();
  forall_symbols(it, tmp_symbol_table.symbols)
  {
    // add __CPROVER_assume if necessary (it might exist already)
    if(it->first=="__CPROVER_assume")
      symbol_table.add(it->second);
    else if(it->first=="__CPROVER_initialize")
    {
      value=it->second.value;

      replace_symbolt replace;
      replace.insert("ARGC", ns.lookup("argc'").symbol_expr());
      replace.insert("ARGV", ns.lookup("argv'").symbol_expr());
      replace(value);
    }
    else if(has_prefix(id2string(it->first),
                       "__CPROVER_initialize::") &&
            symbol_table.add(it->second))
      assert(false);
  }

  assert(value.is_not_nil());
  goto_convert(
    to_code(value),
    symbol_table,
    tmp,
    message_handler);
  Forall_goto_program_instructions(it, tmp)
  {
    it->source_location.set_file("<built-in-library>");
    it->function="__CPROVER_initialize";
  }
  init.insert_before_swap(init_end, tmp);

  // update counters etc.
  remove_skip(init);
  init.compute_loop_numbers();
  goto_functions.update();

  return false;
}

