/*******************************************************************\

Module: Memory Regions

Author: Michael Tautschnig, mt@eecs.qmul.ac.uk

\*******************************************************************/

#ifndef CPROVER_POINTER_ANALYSIS_MEMORY_REGIONS_H
#define CPROVER_POINTER_ANALYSIS_MEMORY_REGIONS_H

#include <util/guard.h>

/*! \brief TO_BE_DOCUMENTED
*/
class memory_regionst
{
public:
  void add_static_objects(
    const symbol_tablet &symbols,
    const goto_functionst &goto_functions);

  void record_decl(const symbol_exprt &expr, const guardt &guard);
  void record_dead(const symbol_exprt &expr, const guardt &guard);
  void record_malloc(const symbol_exprt &expr, const guardt &guard);
  void record_free(const symbol_exprt &expr, const guardt &guard);

  typedef hash_map_cont<symbol_exprt, guardt, irep_hash> live_objectst;

  inline const live_objectst &get_memory_regions() const
  {
    return live_objects;
  }

protected:
  live_objectst live_objects;

  void add_strings(const goto_functionst &goto_functions);
};

#endif
