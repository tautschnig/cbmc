/*******************************************************************\

Module: Symbolic Execution of ANSI-C

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

/// \file
/// Symbolic Execution of ANSI-C

#include "goto_symex.h"

void goto_symext::symex_set_return_value(
  statet &state,
  const exprt &return_value)
{
  // we must be inside a function that was called
  PRECONDITION(!state.call_stack().empty());

  framet &frame = state.call_stack().top();
  if(frame.return_value_symbol.has_value())
  {
    // Cast return value to match the return_value_symbol type if needed
    auto casted_return_value = typecast_exprt::conditional_cast(
      return_value, frame.return_value_symbol.value().type());
    symex_assign(state, frame.return_value_symbol.value(), casted_return_value);
  }
}
