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
    explicit spawned_funst(
      goto_functionst &goto_functions
    );

    /**@brief The name of the function spawned by the call to
     *        pthread_create.
     * @param   pthread_create the pthread_create instruction.
     * @return  the name of the function spawned by the call to
     *          pthread_create, or the empty string (`""`) if the
     *          third argument to pthread_create is not a straight
     *          function pointer.
     *          (This is a bug. We need to deal with any kind of
     *          argument passed to pthread_create, so add additional
     *          cases if you find them.)
     * @post    After this function runs, any calls to result() on
     *          this spawned_funst will return 0 if there were no
     *          problems with pthread_create third-parameters as
     *          described above. Else, the +number+ of such
     *          occurrences is returned by a call to result().
     * @throws  An exception if the instruction doesn't appear to be a
     *          pthread_create call.
     */
    std::string function_pointer_of_pthread_create(
        goto_programt::instructiont &pthread_create
        );

    ///@brief The list of spawned functions formatted as JSON
    std::string to_json();

    /**@brief   Returns the number of pthread_create anomalies
     *          occurred when constructing this spawed_funst.
     * @sa      the documentation for function_pointer_of_pthread_create()
     *          for more details.
     */
    int result(){ return num_of_anomalies; }

  private:
    goto_functionst &goto_functions;

    int num_of_anomalies;

    std::set<std::string> spawned_functions;

    typedef goto_functionst::function_mapt function_mapt;
    typedef goto_programt::instructiont instructiont;
    typedef goto_programt::instructionst instructionst;
    typedef code_function_callt::argumentst argumentst;
};

#endif
