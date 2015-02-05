/** @file   transitive_calls.h
 *  @author Kareem Khazem <karkhaz@karkhaz.com>
 *  @date   2014
 *  @brief  Calculating transitive call chains
 *
 *  If making changes to this file, please run the regression tests at
 *  regression/transitive_calls. The tests should all pass.
 *
 *  If you discover a bug, please add a regression that fails due to
 *  the bug.
 */
#ifndef CPROVER_TRANSITIVE_CALLS_H
#define CPROVER_TRANSITIVE_CALLS_H

#include <ostream>
#include <sstream>
#include <map>
#include <set>
#include "goto_functions.h"
#include "goto_program.h"

/**@brief Calculating transitive call chains
 *
 * This class is used to figure out what functions are transitively
 * called by a given function. When constructed with a goto_functionst
 * object, this transitive_callst will calculate all functions
 * transitively called by each function in the goto_functionst.
 */
class transitive_callst
{
  public:
    /**@brief For each C function in goto_functions, calculates what
     *        functions are transitively called by that C function.
     */
    void operator()();

    transitive_callst(
      goto_functionst &goto_functions
    , namespacet &ns
    );

    ///@brief The transitive call information formatted as JSON
    std::string to_json();

  private:
    goto_functionst &goto_functions;
    namespacet &ns;

    typedef std::string namet;

    /**@brief a list of function names */
    typedef std::list<namet> fun_list;

    typedef goto_functionst::function_mapt function_mapt;
    typedef goto_programt::instructionst instructionst;
    typedef goto_programt::instructiont instructiont;

    void populate_initial(fun_list &worklist);
    void propagate_calls(fun_list  &worklist);

    namet function_of_instruction(const instructiont &instruction);

    /**@brief a map from functions to a bucket of functions
     *        transitively called
     */
    typedef std::map<namet, std::set<namet> >
      call_mapt;
    ///@brief transitive call information
    call_mapt call_map;

    typedef code_function_callt::argumentst argumentst;

    bool not_interested_in(const namet &function);

};

#endif
