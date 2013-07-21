/*******************************************************************\

Module: Field-insensitive, location-sensitive may-alias analysis

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#ifndef CPROVER_MAY_ALIAS_H
#define CPROVER_MAY_ALIAS_H

#include "local_may_alias.h"

/*******************************************************************\

   Class: may_aliast
   
 Purpose:

\*******************************************************************/

class may_aliast:public local_may_aliast
{
public:
  may_aliast(
    const goto_functionst &goto_functions,
    const namespacet &ns);

  void output(
    std::ostream &out,
    const goto_functionst &goto_functions) const;

protected:
  virtual bool is_tracked(const irep_idt &identifier) const;
};

#endif
