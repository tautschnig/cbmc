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
    typedef std::list<namet> name_listt;
    typedef std::set<namet>  name_sett;

    typedef goto_functionst::function_mapt function_mapt;
    typedef goto_programt::instructionst instructionst;
    typedef goto_programt::instructiont instructiont;

    /**@brief Populates call_map with immediate calls.
     * @param worklist an empty list
     * @post  The keys of call_map shall be the names of all functions
     *        in goto_functions. Each function name F shall map to a
     *        set of functions called from the body of F. worklist
     *        shall be a list of all function names */
    void populate_initial(name_listt &worklist);

    /**@brief Calculates transitive call information.
     * @param updated a list of all function names in goto_functions
     * @pre   populate_initial() has been called
     * @post  The keys of call_map shall be the list of all functions
     *        in goto_functions. Each function F shall map to a set of
     *        functions _transitively_ called by F. updated shall be
     *        empty. */
    void propagate_calls(name_listt  &worklist);

    /**@brief The function name of a function call instruction
     *
     *        For most function calls, this simply returns the
     *        function name. If the function call is a call to
     *        pthread_create, return the _spawned_ function instead
     *        (i.e. the third argument to pthread_create).
     */
    namet function_of_instruction(const instructiont &instruction);

    /**@brief a map from functions to a bucket of functions
     *        transitively called
     */
    typedef std::map<namet, std::set<namet> >
      call_mapt;
    ///@brief transitive call information
    call_mapt call_map;

    typedef code_function_callt::argumentst argumentst;

    /**@brief Should we exclude function from the mapping? */
    inline bool not_interested_in(const namet &function);
};

#endif
