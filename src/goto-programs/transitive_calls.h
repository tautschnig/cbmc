/** @file   transitive_calls.h
 *  @author Kareem Khazem <karkhaz@karkhaz.com>
 *  @date   2014
 *  @brief  Calculating transitive call chains
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
    transitive_callst(
      goto_functionst &goto_functions
    , namespacet &ns
    );

    ///@brief The transitive call information formatted as JSON
    std::string to_json();

  protected:
    /**@brief Generates the transitive call bucket for `function`.
     * @param fun_name The name of the function; used as the key in
     *        call_map
     * @param function The actual function, whose body will be scanned
     *        for function calls
     * @return The call bucket for `function`.
     */
    std::set<std::string> make_call_bucket_for(
      std::string fun_name
    );

  private:
    goto_functionst &goto_functions;
    namespacet &ns;

    std::set<std::string> make_call_bucket_for(
      std::set<std::string> worklist
    , std::set<std::string> accumulator
    , bool first_call
    );

    /**@brief a map from functions to a bucket of functions
     *        transitively called
     */
    typedef std::map<std::string, std::set<std::string> >
      call_mapt;
    ///@brief transitive call information
    call_mapt call_map;

    typedef std::set<std::string> visitedt;
    /**@brief All functions for which we have already generated a
     *        call bucket
     */
    visitedt visited;

    typedef goto_functionst::function_mapt function_mapt;
    typedef goto_programt::instructionst instructionst;
    typedef goto_programt::instructiont instructiont;

    ///@{ \name printing
    //
    const int indent_width; // int (not unsigned) so that we detect
    int indent_level;       // negatives.

    /**@brief newline, plus increase indentation
     *
     *  Style note: put the ind() just after where you want the line
     *  to be broken, like ss << "end of line here" << ind();
     */
    std::string ind()
    {
      assert(indent_level >= 0);
      std::stringstream ss;
      indent_level += 1;
      ss << "\n";
      for(int i = 0;
          i < (indent_level * indent_width);
          i++)
        ss << " ";

      return ss.str();
    }

    /**@brief newline, plus decrease indentation
     *
     *  Style note: put the und() just BEFORE the start of the new
     *  line, like ss << und() << "start of new line";
     */
    std::string und()
    {
      assert(indent_level >= 1);
      indent_level -= 1;
      std::stringstream ss;
      ss << "\n";
      for(int i = 0;
          i < (indent_level * indent_width);
          i++)
        ss << " ";

      return ss.str();
    }

    /**@brief newline, same indentation as before
     *
     *  Style note: put the nl() just BEFORE the start of the new
     *  line, like ss << nl() << "start of new line";
     */
    std::string nl()
    {
      assert(indent_level >= 0);
      std::stringstream ss;
      ss << "\n";
      for(int i = 0;
          i < (indent_level * indent_width);
          i++)
        ss << " ";

      return ss.str();
    }
    ///@}
};

#endif
