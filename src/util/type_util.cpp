/*******************************************************************\

Module:

Author: Michael Tautschnig, michael.tautschnig@cs.ox.ac.uk

\*******************************************************************/

#include "type_util.h"

#include "config.h"
#include "namespace.h"
#include "std_types.h"

/*******************************************************************\

Function: is_char_type

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool is_char_type(const typet &type, const namespacet &ns)
{
  if(type.id()==ID_symbol)
    return is_char_type(ns.follow(type), ns);

  if(type.id()!=ID_signedbv &&
      type.id()!=ID_unsignedbv)
    return false;

  return to_bitvector_type(type).get_width()==config.ansi_c.char_width;
}

