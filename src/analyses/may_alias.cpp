/*******************************************************************\

Module: Field-insensitive, location-sensitive may-alias analysis

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <cassert>

#include "may_alias.h"

/*******************************************************************\

Function: may_aliast::may_aliast

  Inputs:

 Outputs:

 Purpose: 

\*******************************************************************/

may_aliast::may_aliast(
  const goto_functionst &goto_functions,
  const namespacet &ns):
  local_may_aliast(goto_functionst::goto_functiont(), ns, false)
{
  assert(!goto_functions.function_map.empty());

  dirty=dirtyt(goto_functions);
  forall_goto_functions(it, goto_functions)
  {
    locals=localst(it->second);
    cfg=local_cfgt(it->second.body);

    build(it->second);
  }

  // make it context-insensitive for all non-local variables
  // relies on the fact that local_may_aliast is flow-insensitive for
  // non-local variables
  points_tot merged;
  for(numbering<irep_idt>::const_iterator it=pointers.begin();
      it!=pointers.end();
      ++it)
    if(!local_may_aliast::is_tracked(*it))
      merged.insert(std::make_pair(pointers.number(*it), destt()));

  // build the merged destt
  forall_goto_functions(it, goto_functions)
  {
    goto_programt::const_targett last=
      --(it->second.body.instructions.end());
    const loc_infot &loc_info=loc_infos[last];

    for(points_tot::iterator m_it=merged.begin();
        m_it!=merged.end();
        ++m_it)
    {
      points_tot::const_iterator entry=
        loc_info.points_to.find(m_it->first);
      if(entry!=loc_info.points_to.end())
        m_it->second.merge(entry->second);
    }
  }

  // update points_to at all locations
  forall_goto_functions(it, goto_functions)
    forall_goto_program_instructions(l_it, it->second.body)
    {
      loc_infot &loc_info=loc_infos[l_it];

      for(points_tot::iterator m_it=merged.begin();
          m_it!=merged.end();
          ++m_it)
        loc_info.points_to[m_it->first]=m_it->second;
    }
}

/*******************************************************************\

Function: may_aliast::is_tracked

  Inputs:

 Outputs: return 'true' iff we track the object with given
          identifier

 Purpose:

\*******************************************************************/

bool may_aliast::is_tracked(const irep_idt &identifier) const
{
  if(ns.lookup(identifier).type.id()!=ID_pointer) return false;
  if(dirty.is_dirty(identifier)) return false;
  return true;
}

/*******************************************************************\

Function: may_aliast::output

  Inputs:

 Outputs:

 Purpose: 

\*******************************************************************/

void may_aliast::output(
  std::ostream &out,
  const goto_functionst &goto_functions) const
{
  forall_goto_functions(it, goto_functions)
  {
    out << ">>>>" << std::endl;
    out << ">>>> " << it->first << std::endl;
    out << ">>>>" << std::endl;
    local_may_aliast::output(out, it->second);
    out << std::endl;
  }
}

