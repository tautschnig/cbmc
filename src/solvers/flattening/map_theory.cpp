/*******************************************************************\

Module: Map Theory (base class for array theory)

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

/// \file
/// Map Theory — constructor

#include "map_theory.h"

map_theoryt::map_theoryt(
  const namespacet &_ns,
  propt &_prop,
  message_handlert &_message_handler,
  bool _get_array_constraints)
  : equalityt(_prop, _message_handler),
    ns(_ns),
    log(_message_handler),
    lazy_arrays(false),
    incremental_cache(false),
    get_array_constraints(_get_array_constraints)
{
}
