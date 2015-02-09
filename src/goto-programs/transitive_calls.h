/** @file   transitive_calls.h
 *  @author Kareem Khazem <karkhaz@karkhaz.com>
 *  @date   2015
 *
 *  If making changes to this file, please run the regression tests at
 *  regression/transitive_calls. The tests should all pass.
 *
 *  If you discover a bug, please add a regression that fails due to
 *  the bug.
 */
#ifndef TRANSITIVE_CALLS_H
#define TRANSITIVE_CALLS_H

#include "goto_functions.h"
#include "goto_program.h"

#include <util/graph.h>

#include <iostream>

/**@brief Calculating transitive call chains
 *
 * This class is used to figure out what functions are transitively
 * called by a given function. When constructed with a goto_functionst
 * object, this transitive_callst will calculate all functions
 * transitively called by each function in the goto_functionst.
 *
 * This class is aware of function spawns using pthread_create. In
 * other words, if function foo contains a call to pthread_create(...,
 * ..., bar, ...), then the analysis will indicate that foo
 * transitively calls bar (and all the functions that bar transitively
 * calls). The analysis will _not_ indicate that foo transitively
 * calls pthread_create.
 *
 * Regression tests for this class are found in
 * regression/transitive_calls.
 */
class transitive_callst
{
  public:
    transitive_callst(
      namespacet &_ns,
      goto_functionst &_goto_functions
    ): g(), ns(_ns), goto_functions(_goto_functions),
       fun2graph()
    {}

    /**@brief Computes the call graph for the goto_functions.
     * @pre   The constructor has been called
     * @post  This graph_transitivet shall be ready to have one of the
     *        output functions called on it
     */
    void operator()();

    /**@brief Outputs a DOT-formatted call graph to out.
     * @pre   operator()() has been called
     * @post  This function has no side effects apart from output.
     */
    void output_dot(std::ostream &out);

    /**@brief Outputs a JSON-formatted representation of the
     *        transitive calls.
     * @pre   operator()() has been called
     * @post  This function has no side effects apart from output. In
     *        particular, the transitive calls are computed on a
     *        function-by-function basis and are not cached, in order
     *        to save memory.
     *
     * The format of the output is like:
     *
     *     [
     *       {
     *         "function_name" : "foo",
     *         "called_functions" : [
     *           "bar",
     *           ...
     *         ],
     *       },
     *       ...
     *     ]
     *
     */
    void output_json(std::ostream &out);

  private:
    typedef goto_programt::instructiont instructiont;
    typedef goto_programt::instructionst instructionst;
    typedef goto_functionst::function_mapt function_mapt;

    typedef std::string namet;
    typedef std::list<namet> namest;

    typedef code_function_callt::argumentst argumentst;

    class nodet: public graph_nodet<empty_edget>
    {
        namet fun_name;

      public:

        bool visited;

        nodet()
          :visited(false){}

        namet &name(){ return fun_name; }
    };

    typedef graph<nodet> grapht;

    grapht g;

    namespacet &ns;
    goto_functionst &goto_functions;

    /**@brief Function names -> graph nodes */
    std::map<namet, unsigned> fun2graph;

    /**@brief Creates a node for each function in the graph */ 
    void add_functions();
    /**@brief Creates an edge for each function call in the graph */ 
    void add_calls();

    /**@brief The function name of a function call instruction
     * @note  In this implementation, the returned name for a call to
     *        pthread_create will +not+ be `pthread_create`. Instead,
     *        this function will return the name of the function
     *        +spawned+ by the call to pthread_create.
     * @throws exception if `instruction` is a call to pthread_create,
     *         and the third argument to pthread_create is not a
     *         straight function pointer (i.e. it's a pointer to an
     *         FP, or something more exotic). This should probably be
     *         handled better.
     */
    namet name_of_call(const instructiont &instruction);

    /**@brief Whether we should care about adding a function name to
     *        the transitive call graph. False for CPROVER builtins.
     */
    bool not_interested_in(namet fun_name);

    /**@brief Fills `calls` with a list of functions that are
     *        transitively called by `function`.
     *
     * @note  the considerations about pthread_create in the
     *        documentation for name_of_call().
     */
    void get_transitive_calls(namet &function, namest &calls);

    /**@brief Removes the `visited` flag on every node in the graph.
     */
    void clear_visited();
};

#endif
