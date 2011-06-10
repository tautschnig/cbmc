/*******************************************************************\

Module:

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <ostream>
#include <cassert>

#include "threeval.h"

const char *tvt::to_string() const
{
  switch(value)
  {
  case tv_enumt::TV_TRUE: return "TRUE";
  case tv_enumt::TV_FALSE: return "FALSE";
  case tv_enumt::TV_UNKNOWN: return "UNKNOWN";
  }

  assert(false);
  return "";
}

std::ostream &operator << (std::ostream &out, const tvt &a)
{
  return out << a.to_string();
}
