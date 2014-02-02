/*******************************************************************\

Module: Internal Representation

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <cassert>

#include "irep_ids.h"
#include "string_container.h"

const char *irep_ids_table[]={
  #include "irep_ids.inc"
  0
};

/*******************************************************************\

Function: initialize_string_container

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void initialize_string_container()
{
  // this is called by the constructor of string_containert
  
  for(size_t i=0; irep_ids_table[i]!=0; i++)
  {
    size_t x=0;
    x=string_container[irep_ids_table[i]];
    assert(x==i); // sanity check
  }
}
