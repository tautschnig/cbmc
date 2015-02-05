#ifndef CPROVER_SPAWNED_FUNS_H
#define CPROVER_SPAWNED_FUNS_H

#include <goto-programs/goto_functions.h>
#include <goto-programs/goto_program.h>

#include <set>

class spawned_funst
{
  public:
    /**@brief Generates a list of functions that are spawned as
     *        threads
     */
    spawned_funst(
      goto_functionst &goto_functions
    , namespacet &ns
    );

    ///@brief The list of spawned functions formatted as JSON
    std::string to_json();

  private:
    goto_functionst &goto_functions;
    namespacet &ns;

    std::set<std::string> spawned_functions;

    typedef goto_functionst::function_mapt function_mapt;
    typedef goto_programt::instructiont instructiont;
    typedef goto_programt::instructionst instructionst;
    typedef code_function_callt::argumentst argumentst;
};

#endif
