/*******************************************************************\

Module: Unwind the counted loops of the Java library models

Author: Diffblue Ltd.

\*******************************************************************/

/// \file
/// Unwind the counted loops of the Java library models

#ifndef CPROVER_JAVA_BYTECODE_JAVA_LIBRARY_LOOP_UNWIND_HANDLER_H
#define CPROVER_JAVA_BYTECODE_JAVA_LIBRARY_LOOP_UNWIND_HANDLER_H

#include <util/irep.h>
#include <util/mp_arith.h>
#include <util/threeval.h>

#include <map>
#include <optional>
#include <utility>

class abstract_goto_modelt;
class call_stackt;

/// The largest number of iterations that java_library_loop_unwind_handler
/// unwinds a counted loop for.
const std::size_t java_library_loop_max_iterations = 4096;

/// Lets symex unwind the loops of the methods of the library (the models of
/// java.*, javax.*, sun.*, jdk.* and org.cprover.*) that count with a
/// constant: those whose head compares a local variable with a constant,
/// whose only assignment in the loop adds a constant to it, and which is
/// assigned a constant just before the loop. Such a loop runs a known number
/// of times, which may exceed --unwind (a model's loop over the 128 bits of
/// a BigInteger, say): it is unwound that often (at most
/// java_library_loop_max_iterations) whatever --unwind says. This never
/// loses an unwinding assertion: where the count is wrong, symex leaves the
/// loop by its condition first, or the other handlers and --unwind decide
/// beyond it. Other loops are left to them.
class java_library_loop_unwind_handlert
{
public:
  explicit java_library_loop_unwind_handlert(abstract_goto_modelt &goto_model)
    : goto_model(goto_model)
  {
  }

  tvt operator()(
    const call_stackt &context,
    unsigned loop_number,
    unsigned unwind_count,
    unsigned &unwind_max);

  /// The number of iterations of the loop \p loop_number of the function
  /// \p function, if it is a counted loop (see the class).
  std::optional<mp_integer>
  iterations(const irep_idt &function, unsigned loop_number);

private:
  abstract_goto_modelt &goto_model;
  std::map<std::pair<irep_idt, unsigned>, std::optional<mp_integer>> cache;
};

#endif // CPROVER_JAVA_BYTECODE_JAVA_LIBRARY_LOOP_UNWIND_HANDLER_H
