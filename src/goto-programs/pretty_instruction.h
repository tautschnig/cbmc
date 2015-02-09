/** @file   pretty_instruction.h
 * @author  Kareem Khazem <karkhaz@karkhaz.com>
 * @date    2014
 * @brief   Pretty printing of instructionts
 */

#ifndef CPROVER_PRETTY_INSTRUCTION_H
#define CPROVER_PRETTY_INSTRUCTION_H

#include <goto-programs/goto_functions.h>
#include <string>
#include <sstream>

/** @brief Base class for instruction pretty-printers
 *
 * This class provides an interface for pretty-printing objects of
 * class instructiont. There are several contexts where a
 * human-readable representation of instructions is required: on the
 * nodes of a CFG, CLI output, etc.
 *
 * It is also useful for a particular pretty-printer to be reusable
 * across several implementations. For example, a pretty-printer
 * suitable for one CFG might be suitable for another, possibly with a
 * few modifications.
 *
 * In order to create a pretty-printer, create a derived class of
 * pretty_instruction_baset and override the virtual methods.
 *
 * @nosubgrouping
 */
class pretty_instruction_baset
{
public:
  /** @brief  Pretty-prints `instruction`. The exact format depends on
   *          the actual class of this pretty-printer.
   *
   *  @attention Derived classes should not override/hide this method.
   */
  std::string operator()(goto_programt::const_targett instruction);

  /**@{ @name Printer-specific functions
   * Derived classes should override these functions to provide
   * suitable formating for instructions.
   *
   * Note that strings should not be modified with escape characters
   * in the `pretty_*` methods. The return value from each of the
   * `pretty_*` methods will be escaped using the code in the
   * `escape()` method.
   */
protected:
  /** @brief  Escapes troublesome characters in `str`
   *
   * The code for instructions might contain characters that are
   * unsuitable for whatever this pretty-printer is targeted for. For
   * example, if we are pretty-printing instructions to a DOT file, we
   * should replace `\n` characters with the string `"\n"`. Derivers
   * of this class should override this function to deal with whatever
   * escapes they require.
   *
   * @return  A version of `str`, except that characters that are
   *          unsuitable for this pretty-printer are escaped or
   *          modified.
   */
  virtual std::string escape(std::string str) = 0;

  /**@brief A string to be prepended to every instruction printed
   *        using this pretty-printer
   */
  virtual std::string pre_string(goto_programt::const_targett ins)
  { return ""; }
  /**@brief A string to be appended to every instruction printed
   *        using this pretty-printer
   */
  virtual std::string post_string(goto_programt::const_targett ins)
  { return ""; }

  virtual std::string
  pretty_goto(goto_programt::const_targett instruction) = 0;
  virtual std::string
  pretty_return(goto_programt::const_targett instruction) = 0;
  virtual std::string
  pretty_assign(goto_programt::const_targett instruction) = 0;
  virtual std::string
  pretty_function_call(goto_programt::const_targett instruction) = 0;
  virtual std::string
  pretty_throw(goto_programt::const_targett instruction) = 0;
  virtual std::string
  pretty_catch(goto_programt::const_targett instruction) = 0;
  virtual std::string
  pretty_skip(goto_programt::const_targett instruction) = 0;
  virtual std::string
  pretty_location(goto_programt::const_targett instruction) = 0;
  virtual std::string
  pretty_other(goto_programt::const_targett instruction) = 0;
  virtual std::string
  pretty_decl(goto_programt::const_targett instruction) = 0;
  virtual std::string
  pretty_dead(goto_programt::const_targett instruction) = 0;
  virtual std::string
  pretty_assume(goto_programt::const_targett instruction) = 0;
  virtual std::string
  pretty_assert(goto_programt::const_targett instruction) = 0;
  virtual std::string
  pretty_atomic_begin(goto_programt::const_targett instruction) = 0;
  virtual std::string
  pretty_atomic_end(goto_programt::const_targett instruction) = 0;
  virtual std::string
  pretty_start_thread(goto_programt::const_targett instruction) = 0;
  virtual std::string
  pretty_end_thread(goto_programt::const_targett instruction) = 0;
  virtual std::string
  pretty_end_function(goto_programt::const_targett instruction) = 0;
  ///@}
  
};

/**@brief Generic pretty-printing of instructions
 *
 * This pretty-printer outputs a prettier representation of
 * instructions suitable for debugging. Calling operator()() on an
 * instruction returns a prettified representation of it.
 */
class plain_pretty_instructiont: public pretty_instruction_baset
{

protected:
  namespacet ns;

public:
  plain_pretty_instructiont(namespacet _ns): ns(_ns) {}

protected:
  std::string pretty_goto(goto_programt::const_targett instruction);
  std::string pretty_return(goto_programt::const_targett instruction);
  std::string pretty_assign(goto_programt::const_targett instruction);
  std::string pretty_function_call(goto_programt::const_targett instruction);
  std::string pretty_throw(goto_programt::const_targett instruction);
  std::string pretty_catch(goto_programt::const_targett instruction);
  std::string pretty_skip(goto_programt::const_targett instruction);
  std::string pretty_location(goto_programt::const_targett instruction);
  std::string pretty_other(goto_programt::const_targett instruction);
  std::string pretty_decl(goto_programt::const_targett instruction);
  std::string pretty_dead(goto_programt::const_targett instruction);
  std::string pretty_assume(goto_programt::const_targett instruction);
  std::string pretty_assert(goto_programt::const_targett instruction);
  std::string pretty_atomic_begin(goto_programt::const_targett instruction);
  std::string pretty_atomic_end(goto_programt::const_targett instruction);
  std::string pretty_start_thread(goto_programt::const_targett instruction);
  std::string pretty_end_thread(goto_programt::const_targett instruction);
  std::string pretty_end_function(goto_programt::const_targett instruction);
  
  /**@brief Returns `str` unmodified. */
  inline std::string escape(std::string str);
  /**@brief Prepends nothing to the pretty representation of
   * `instruction`.
   */
  inline std::string pre_string(goto_programt::const_targett instruction);
  /**@brief Appends nothing to the pretty representation of
   * `instruction`.
   */
  inline std::string post_string(goto_programt::const_targett instruction);
};

/**@brief Pretty-printing instructions to DOT files
 *
 * This pretty-printer outputs instructions in a format suitable for
 * adding to the labels of DOT-formatted files. Calling operator()()
 * on an instruction returns a prettified version of that instruction,
 * including all necessary escape sequences.
 */
class cfg_pretty_instructiont: public plain_pretty_instructiont
{

public:
  cfg_pretty_instructiont(namespacet _ns):
    plain_pretty_instructiont(_ns) {}

protected:
  /**@brief Returns a version of `str` suitable for DOT files. */
  std::string escape(std::string str);
  /**@brief Prepends `instruction`'s location number to the pretty
   * representation of `instruction`.
   */
  std::string pre_string(goto_programt::const_targett instruction);
  /**@brief Appends nothing to the pretty representation of
   * `instruction`.
   */
  std::string post_string(goto_programt::const_targett instruction);
};

#endif
