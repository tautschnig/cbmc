/*******************************************************************\

Module: Range-based reaching definitions analysis (following Field-
        Sensitive Program Dependence Analysis, Litvak et al., FSE 2010)

Author: Michael Tautschnig

Date: February 2013

\*******************************************************************/

#include <util/pointer_offset_size.h>
#include <util/prefix.h>

#include <pointer-analysis/rd_dereference.h>

#include "is_threaded.h"
#include "dirty.h"

#include "may_alias.h"

#include "reaching_definitions.h"

/*******************************************************************\

Function: rd_range_domaint::populate_cache

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

<<<<<<< HEAD
void rd_range_domaint::populate_cache(const irep_idt &identifier) const
{
  assert(bv_container);

  valuest::const_iterator v_entry=values.find(identifier);
  if(v_entry==values.end() ||
     v_entry->second.empty())
    return;

  ranges_at_loct &export_entry=export_cache[identifier];

  for(values_innert::const_iterator
      it=v_entry->second.begin();
      it!=v_entry->second.end();
=======
void rd_range_domaint::populate_cache() const
{
  if(export_cache_available) return;

  export_cache.clear();
  assert(bv_container);

  for(valuest::const_iterator it=values.begin();
      it!=values.end();
>>>>>>> 9de64cc... Implement reaching definitions as sparse bitvector analysis for memory efficiency
      ++it)
  {
    const reaching_definitiont &v=bv_container->get(*it);

<<<<<<< HEAD
<<<<<<< HEAD
    export_entry[v.definition_at].insert(
      std::make_pair(v.bit_begin, v.bit_end));
  }
=======
    export_cache[v.identifier][v.definition_at][v.bit_begin]=
      v.bit_end;
=======
    export_cache[v.identifier][v.definition_at].insert(
      std::make_pair(v.bit_begin, v.bit_end));
>>>>>>> 62f8b91... debugging reachdef
  }

  export_cache_available=true;
>>>>>>> 9de64cc... Implement reaching definitions as sparse bitvector analysis for memory efficiency
}

/*******************************************************************\

Function: rd_range_domaint::transform

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void rd_range_domaint::transform(
  locationt from,
  locationt to,
  ai_baset &ai,
  const namespacet &ns)
{
  reaching_definitions_analysist *rd=
    dynamic_cast<reaching_definitions_analysist*>(&ai);
  assert(rd!=0);

  assert(bv_container);

  /*if(my_loc->location_number==12)
    output(std::cerr);*/

  // kill values
  if(from->is_dead())
    transform_dead(ns, from);
  // kill thread-local values
  else if(from->is_start_thread())
    transform_start_thread(ns, *rd);
  // do argument-to-parameter assignments
  else if(from->is_function_call())
    transform_function_call(ns, from, to, *rd);
  // cleanup parameters
  else if(from->is_end_function())
    transform_end_function(ns, from, to, *rd);
  // lhs assignements
  else if(from->is_assign())
    transform_assign(ns, from, from, *rd);
  // initial (non-deterministic) value
  else if(from->is_decl())
    transform_assign(ns, from, from, *rd);

  /*if(my_loc->location_number==12)
    output(std::cerr);*/

#if 0
  // handle return values
  if(to->is_function_call())
  {
    const code_function_callt &code=to_code_function_call(to->code);

    if(code.lhs().is_not_nil())
    {
      rw_range_set_dereft rw_set(ns, rd->get_rd_dereference());
      goto_rw(to, rw_set);
      const bool is_must_alias=rw_set.get_w_set().size()==1;

      forall_rw_range_set_w_objects(it, rw_set)
      {
        const irep_idt &identifier=it->first;
        // ignore symex::invalid_object
        const symbolt *symbol_ptr;
        if(ns.lookup(identifier, symbol_ptr))
          continue;
        assert(symbol_ptr!=0);

        const range_domaint &ranges=rw_set.get_ranges(it);

        if(is_must_alias &&
           (!rd->get_is_threaded()(from) ||
            (!symbol_ptr->is_shared() &&
             !rd->get_is_dirty()(identifier))))
          for(range_domaint::const_iterator
              r_it=ranges.begin();
              r_it!=ranges.end();
              ++r_it)
            kill(identifier, r_it->first, r_it->second);
      }
    }
  }
#endif
}

/*******************************************************************\

Function: rd_range_domaint::transform_dead

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void rd_range_domaint::transform_dead(
  const namespacet &ns,
  locationt from)
{
  const irep_idt& identifier=
    to_symbol_expr(to_code_dead(from->code).symbol()).get_identifier();

<<<<<<< HEAD
  valuest::iterator entry=values.find(identifier);

  if(entry!=values.end())
  {
    values.erase(entry);
    export_cache.erase(identifier);
  }
=======
  for(valuest::iterator it=values.begin();
      it!=values.end();
      ) // no ++it
    if(bv_container->get(*it).identifier==identifier)
    {
      export_cache_available=false;

      valuest::iterator next=it;
      ++next;
      values.erase(it);
      it=next;
    }
    else
      ++it;
>>>>>>> 9de64cc... Implement reaching definitions as sparse bitvector analysis for memory efficiency
}

/*******************************************************************\

Function: rd_range_domaint::transform_start_thread

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void rd_range_domaint::transform_start_thread(
  const namespacet &ns,
  reaching_definitions_analysist &rd)
{
  for(valuest::iterator it=values.begin();
      it!=values.end();
      ) // no ++it
  {
<<<<<<< HEAD
    const irep_idt &identifier=it->first;
=======
    const irep_idt &identifier=bv_container->get(*it).identifier;
>>>>>>> 9de64cc... Implement reaching definitions as sparse bitvector analysis for memory efficiency

    if(!ns.lookup(identifier).is_shared() &&
       !rd.get_is_dirty()(identifier))
    {
<<<<<<< HEAD
      export_cache.erase(identifier);
=======
      export_cache_available=false;
>>>>>>> 9de64cc... Implement reaching definitions as sparse bitvector analysis for memory efficiency

      valuest::iterator next=it;
      ++next;
      values.erase(it);
      it=next;
    }
    else
      ++it;
  }
}

/*******************************************************************\

Function: rd_range_domaint::transform_function_call

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void rd_range_domaint::transform_function_call(
  const namespacet &ns,
  locationt from,
  locationt to,
  reaching_definitions_analysist &rd)
{
<<<<<<< HEAD
  const code_function_callt &code=to_code_function_call(from->code);

  goto_programt::const_targett next=from;
  ++next;
=======
  assert(may_alias);

  const exprt &pointer=deref.pointer();
  std::set<exprt> alias_set=may_alias->get(from, pointer);
>>>>>>> 58b702d... Changed --show-local-may-alias to use may_aliast, reach-def uses may_aliast

  // only if there is an actual call, i.e., we have a body
  if(next!=to)
  {
    for(valuest::iterator it=values.begin();
        it!=values.end();
       ) // no ++it
    {
<<<<<<< HEAD
      const irep_idt &identifier=it->first;
=======
      const irep_idt &identifier=bv_container->get(*it).identifier;
>>>>>>> 9de64cc... Implement reaching definitions as sparse bitvector analysis for memory efficiency

      // dereferencing may introduce extra symbols
      const symbolt *sym;
      if((ns.lookup(identifier, sym) ||
          !sym->is_shared()) &&
         !rd.get_is_dirty()(identifier))
      {
<<<<<<< HEAD
        export_cache.erase(identifier);
=======
        export_cache_available=false;
>>>>>>> 9de64cc... Implement reaching definitions as sparse bitvector analysis for memory efficiency

        valuest::iterator next=it;
        ++next;
        values.erase(it);
        it=next;
      }
      else
        ++it;
    }

    const symbol_exprt& fn_symbol_expr=to_symbol_expr(code.function());
    const code_typet &code_type=
      to_code_type(ns.lookup(fn_symbol_expr.get_identifier()).type);

    const code_typet::parameterst &parameters=code_type.parameters();
    for(code_typet::parameterst::const_iterator it=parameters.begin();
        it!=parameters.end();
        ++it)
    {
      const irep_idt &identifier=it->get_identifier();

      if(identifier.empty())
        continue;

      range_spect size=
        to_range_spect(pointer_offset_bits(it->type(), ns));
      gen(from, identifier, 0, size);
    }
  }
  else
  {
    // handle return values of undefined functions
    const code_function_callt &code=to_code_function_call(from->code);

    if(code.lhs().is_not_nil())
      transform_assign(ns, from, from, rd);
  }
}

/*******************************************************************\

Function: rd_range_domaint::transform_end_function

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

//int do_print=0;
void rd_range_domaint::transform_end_function(
  const namespacet &ns,
  locationt from,
  locationt to,
  reaching_definitions_analysist &rd)
{
  goto_programt::const_targett call=to;
  --call;
  const code_function_callt &code=to_code_function_call(call->code);

  export_cache_available=false;

  valuest new_values;
  new_values.swap(values);
  values=rd[call].values;

  for(valuest::const_iterator
      it=new_values.begin();
      it!=new_values.end();
      ++it)
  {
    // local copy as kill might cause inconsistencies
    reaching_definitiont v=bv_container->get(*it);

    if(!rd.get_is_threaded()(call) ||
<<<<<<< HEAD
       (!ns.lookup(identifier).is_shared() &&
        !rd.get_is_dirty()(identifier)))
<<<<<<< HEAD
      for(values_innert::const_iterator
          i_it=it->second.begin();
          i_it!=it->second.end();
          ++i_it)
      {
        const reaching_definitiont &v=bv_container->get(*i_it);
        kill(v.identifier, v.bit_begin, v.bit_end);
      }

    for(values_innert::const_iterator
        i_it=it->second.begin();
        i_it!=it->second.end();
        ++i_it)
    {
      const reaching_definitiont &v=bv_container->get(*i_it);
      gen(v.definition_at, v.identifier, v.bit_begin, v.bit_end);
    }
=======
      for(ranges_at_loct::const_iterator
          itl=it->second.begin();
          itl!=it->second.end();
          ++itl)
        for(rangest::const_iterator
            r_it=itl->second.begin();
            r_it!=itl->second.end();
            ++r_it)
          kill(identifier, r_it->first, r_it->second);

    for(ranges_at_loct::const_iterator
        itl=it->second.begin();
        itl!=it->second.end();
        ++itl)
      for(rangest::const_iterator
          r_it=itl->second.begin();
          r_it!=itl->second.end();
          ++r_it)
        gen(itl->first, identifier, r_it->first, r_it->second);
>>>>>>> 2442e8f... Changed reaching-definitions data structure for reduced lookup times
=======
       (!ns.lookup(v.identifier).is_shared() &&
        !rd.get_is_dirty()(v.identifier)))
      kill(v.identifier, v.bit_begin, v.bit_end);
  }

  for(valuest::const_iterator
      it=new_values.begin();
      it!=new_values.end();
      ++it)
  {
    reaching_definitiont v=bv_container->get(*it);
    gen(v.definition_at, v.identifier, v.bit_begin, v.bit_end);
<<<<<<< HEAD
<<<<<<< HEAD
>>>>>>> 9de64cc... Implement reaching definitions as sparse bitvector analysis for memory efficiency
=======
    do_print=0;
>>>>>>> 62f8b91... debugging reachdef
=======
    //do_print=0;
>>>>>>> b5cca5d... disable debugging
  }

  const code_typet &code_type=
    to_code_type(ns.lookup(from->function).type);

  const code_typet::parameterst &parameters=code_type.parameters();
  for(code_typet::parameterst::const_iterator p_it=parameters.begin();
      p_it!=parameters.end();
      ++p_it)
  {
<<<<<<< HEAD
    const irep_idt &identifier=p_it->get_identifier();

    if(identifier.empty())
      continue;

    valuest::iterator entry=values.find(identifier);

    if(entry!=values.end())
    {
      values.erase(entry);
      export_cache.erase(identifier);
    }
=======
    if(p_it->get_identifier().empty())
      continue;

    const irep_idt &identifier=p_it->get_identifier();

    for(valuest::iterator it=values.begin();
        it!=values.end();
       ) // no ++it
      if(bv_container->get(*it).identifier==identifier)
      {
        valuest::iterator next=it;
        ++next;
        values.erase(it);
        it=next;
      }
      else
        ++it;
>>>>>>> 9de64cc... Implement reaching definitions as sparse bitvector analysis for memory efficiency
  }

  // handle return values
  if(code.lhs().is_not_nil())
  {
#if 0
    rd_range_domaint *rd_state=
      dynamic_cast<rd_range_domaint*>(&(rd.get_state(call)));
    assert(rd_state!=0);
    rd_state->
#endif
      transform_assign(ns, from, call, rd);
  }
}

/*******************************************************************\

Function: rd_range_domaint::transform_assign

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void rd_range_domaint::transform_assign(
  const namespacet &ns,
  locationt from,
  locationt to,
  reaching_definitions_analysist &rd)
{
  rw_range_set_dereft rw_set(ns, rd.get_rd_dereference());
  goto_rw(from, rw_set);
  const bool is_must_alias=rw_set.get_w_set().size()==1;

  forall_rw_range_set_w_objects(it, rw_set)
  {
    const irep_idt &identifier=it->first;
    // ignore symex::invalid_object
    const symbolt *symbol_ptr;
    if(ns.lookup(identifier, symbol_ptr))
      continue;
    assert(symbol_ptr!=0);

    const range_domaint &ranges=rw_set.get_ranges(it);

    if(is_must_alias &&
       (!rd.get_is_threaded()(from) ||
        (!symbol_ptr->is_shared() &&
         !rd.get_is_dirty()(identifier))))
      for(range_domaint::const_iterator
          r_it=ranges.begin();
          r_it!=ranges.end();
          ++r_it)
      {
        /*if(from->location_number==14 ||
           from->location_number==15)
          std::cerr << "kill@" << from->location_number
            << ":" << identifier << " " << r_it->first
            << "--" << r_it->second << std::endl;*/
        kill(identifier, r_it->first, r_it->second);
      }

    for(range_domaint::const_iterator
        r_it=ranges.begin();
        r_it!=ranges.end();
        ++r_it)
    {
        /*if(from->location_number==14 ||
           from->location_number==15)
          std::cerr << "gen@" << from->location_number
            << ":" << identifier << " " << r_it->first
            << "--" << r_it->second << std::endl;*/
      gen(from, identifier, r_it->first, r_it->second);
    }
  }
}

/*******************************************************************\

Function: rd_range_domaint::kill

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void rd_range_domaint::kill(
  const irep_idt &identifier,
  const range_spect &range_start,
  const range_spect &range_end)
{
  assert(range_start>=0);
  // -1 for objects of infinite/unknown size
  if(range_end==-1)
  {
    kill_inf(identifier, range_start);
    return;
  }

  assert(range_end>range_start);

  valuest new_values;

<<<<<<< HEAD
<<<<<<< HEAD
  bool clear_export_cache=false;
  values_innert new_values;

  for(values_innert::iterator
      it=entry->second.begin();
      it!=entry->second.end();
     ) // no ++it
  {
    const reaching_definitiont &v=bv_container->get(*it);

    if(v.bit_begin >= range_end)
      ++it;
    else if(v.bit_end!=-1 &&
            v.bit_end <= range_start)
      ++it;
    else if(v.bit_begin >= range_start &&
            v.bit_end!=-1 &&
            v.bit_end <= range_end) // rs <= a < b <= re
    {
      clear_export_cache=true;

      entry->second.erase(it++);
    }
    else if(v.bit_begin >= range_start) // rs <= a <= re < b
    {
      clear_export_cache=true;

      reaching_definitiont v_new=v;
      v_new.bit_begin=range_end;
      new_values.insert(bv_container->add(v_new));

      entry->second.erase(it++);
    }
    else if(v.bit_end==-1 ||
            v.bit_end > range_end) // a <= rs < re < b
    {
      clear_export_cache=true;

      reaching_definitiont v_new=v;
      v_new.bit_end=range_start;

      reaching_definitiont v_new2=v;
      v_new2.bit_begin=range_end;

      new_values.insert(bv_container->add(v_new));
      new_values.insert(bv_container->add(v_new2));

      entry->second.erase(it++);
    }
    else // a <= rs < b <= re
    {
      clear_export_cache=true;

      reaching_definitiont v_new=v;
      v_new.bit_end=range_start;
      new_values.insert(bv_container->add(v_new));

      entry->second.erase(it++);
    }
  }

  if(clear_export_cache)
    export_cache.erase(identifier);

  values_innert::iterator it=entry->second.begin();
  for(values_innert::const_iterator
      itn=new_values.begin();
      itn!=new_values.end();
      ++itn)
  {
    while(it!=entry->second.end() && *it<*itn)
      ++it;
    if(it==entry->second.end() || *itn<*it)
    {
      entry->second.insert(it, *itn);
    }
    else if(it!=entry->second.end())
    {
      assert(*it==*itn);
      ++it;
    }
=======
  for(ranges_at_loct::iterator itl=entry->second.begin();
      itl!=entry->second.end();
     ) // no ++itl
=======
  for(valuest::iterator it=values.begin();
      it!=values.end();
     ) // no ++it
>>>>>>> 9de64cc... Implement reaching definitions as sparse bitvector analysis for memory efficiency
  {
    const reaching_definitiont &v=bv_container->get(*it);

    if(v.identifier!=identifier)
      ++it;
    else if(v.bit_begin >= range_end)
      ++it;
    else if(v.bit_end!=-1 &&
            v.bit_end <= range_start)
      ++it;
    else if(v.bit_begin >= range_start &&
            v.bit_end!=-1 &&
            v.bit_end <= range_end) // rs <= a < b <= re
    {
      export_cache_available=false;

      values.erase(it++);
    }
    else if(v.bit_begin >= range_start) // rs <= a <= re < b
    {
      export_cache_available=false;

<<<<<<< HEAD
    if(ranges.empty())
      entry->second.erase(itl++);
    else
      ++itl;
>>>>>>> 2442e8f... Changed reaching-definitions data structure for reduced lookup times
=======
      reaching_definitiont v_new=v;
      v_new.bit_begin=range_end;
      new_values.insert(bv_container->add(v_new));

      values.erase(it++);
    }
    else if(v.bit_end==-1 ||
            v.bit_end > range_end) // a <= rs < re < b
    {
      export_cache_available=false;

      reaching_definitiont v_new=v;
      v_new.bit_end=range_start;

      reaching_definitiont v_new2=v;
      v_new2.bit_begin=range_end;

      new_values.insert(bv_container->add(v_new));
      new_values.insert(bv_container->add(v_new2));

      values.erase(it++);
    }
    else // a <= rs < b <= re
    {
      export_cache_available=false;

      reaching_definitiont v_new=v;
      v_new.bit_end=range_start;
      new_values.insert(bv_container->add(v_new));

      values.erase(it++);
    }
>>>>>>> 9de64cc... Implement reaching definitions as sparse bitvector analysis for memory efficiency
  }

  for(valuest::iterator it=new_values.begin();
      it!=new_values.end();
      ++it)
    export_cache_available=
      !values.insert(*it).second &&
      export_cache_available;
}

/*******************************************************************\

Function: rd_range_domaint::kill_inf

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void rd_range_domaint::kill_inf(
  const irep_idt &identifier,
  const range_spect &range_start)
{
  assert(range_start>=0);

#if 0
  valuest::iterator entry=values.find(identifier);
  if(entry==values.end())
    return;

  XXX export_cache_available=false;

  // makes the analysis underapproximating
  rangest &ranges=entry->second;

  for(rangest::iterator it=ranges.begin();
      it!=ranges.end();
     ) // no ++it
    if(it->second.first!=-1 &&
       it->second.first <= range_start)
      ++it;
    else if(it->first >= range_start) // rs <= a < b <= re
    {
      ranges.erase(it++);
    }
    else // a <= rs < b < re
    {
      it->second.first=range_start;
      ++it;
    }
#endif
}

/*******************************************************************\

Function: rd_range_domaint::gen

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool rd_range_domaint::gen(
  locationt from,
  const irep_idt &identifier,
  const range_spect &range_start,
  const range_spect &range_end)
{
<<<<<<< HEAD
<<<<<<< HEAD
<<<<<<< HEAD
=======
=======
  if(do_print)
=======
  /*if(do_print)
>>>>>>> b5cca5d... disable debugging
  {
    std::cerr << "gen@" << from->location_number << std::endl;
    std::cerr << "range_start=" << range_start << std::endl;
    std::cerr << "range_end=" << range_end << std::endl;
  }*/

>>>>>>> 62f8b91... debugging reachdef
  // objects of size 0 like union U { signed : 0; };
  if(range_start==0 && range_end==0)
    return false;

  assert(range_start>=0);

<<<<<<< HEAD
>>>>>>> 2442e8f... Changed reaching-definitions data structure for reduced lookup times
  // objects of size 0 like union U { signed : 0; };
  if(range_start==0 && range_end==0)
    return false;

  assert(range_start>=0);

=======
>>>>>>> 9de64cc... Implement reaching definitions as sparse bitvector analysis for memory efficiency
  // -1 for objects of infinite/unknown size
  assert(range_end>range_start || range_end==-1);

  reaching_definitiont v;
  v.identifier=identifier;
  v.definition_at=from;
  v.bit_begin=range_start;
  v.bit_end=range_end;
<<<<<<< HEAD

  if(!values[identifier].insert(bv_container->add(v)).second)
    return false;

  export_cache.erase(identifier);

#if 0
  range_spect merged_range_end=range_end;
=======
>>>>>>> 9de64cc... Implement reaching definitions as sparse bitvector analysis for memory efficiency

  if(!values.insert(bv_container->add(v)).second)
  {
    //if(do_print) std::cerr << "Not added" << std::endl;;
    //if(do_print) output(std::cerr);
    return false;
  }
  //if(do_print) output(std::cerr);

<<<<<<< HEAD
<<<<<<< HEAD
<<<<<<< HEAD
<<<<<<< HEAD
  ranges.insert(std::make_pair(
      range_start,
      std::make_pair(merged_range_end, from)));
#endif
=======
  ranges.insert(std::make_pair(range_start, merged_range_end));
>>>>>>> 2442e8f... Changed reaching-definitions data structure for reduced lookup times
=======
  INSERT(ranges, std::make_pair(range_start, merged_range_end));
>>>>>>> a5fe57b... First proposal for reducing memory footprint of RD
=======
  ranges.insert(std::make_pair(range_start, merged_range_end));
>>>>>>> 9aebd47... Revert "First proposal for reducing memory footprint of RD"
=======
  export_cache_available=false;
>>>>>>> 9de64cc... Implement reaching definitions as sparse bitvector analysis for memory efficiency

  return true;
}

/*******************************************************************\

Function: rd_range_domaint::output

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void rd_range_domaint::output(std::ostream &out) const
{
  populate_cache();

  out << "Reaching definitions:" << std::endl;
  for(export_cachet::const_iterator
      it=export_cache.begin();
      it!=export_cache.end();
      ++it)
  {
<<<<<<< HEAD
    const irep_idt &identifier=it->first;

    const ranges_at_loct &ranges=get(identifier);

    out << "  " << identifier << "[";

    for(ranges_at_loct::const_iterator itl=ranges.begin();
        itl!=ranges.end();
=======
    out << "  " << it->first << "[";

    for(ranges_at_loct::const_iterator itl=it->second.begin();
        itl!=it->second.end();
>>>>>>> 2442e8f... Changed reaching-definitions data structure for reduced lookup times
        ++itl)
      for(rangest::const_iterator itr=itl->second.begin();
          itr!=itl->second.end();
          ++itr)
      {
        if(itr!=itl->second.begin() ||
<<<<<<< HEAD
           itl!=ranges.begin())
=======
           itl!=it->second.begin())
>>>>>>> 2442e8f... Changed reaching-definitions data structure for reduced lookup times
          out << ";";

        out << itr->first << ":" << itr->second;
        out << "@" << itl->first->location_number;
      }

    out << "]" << std::endl;
<<<<<<< HEAD

    clear_cache(identifier);
  }
}

/*******************************************************************\

Function: rd_range_domaint::merge_inner

  Inputs:

 Outputs: returns true iff there is s.th. new

 Purpose:

\*******************************************************************/

bool rd_range_domaint::merge_inner(
  values_innert &dest,
  const values_innert &other)
{
  bool more=false;

#if 0
  ranges_at_loct::iterator itr=it->second.begin();
  for(ranges_at_loct::const_iterator itro=ito->second.begin();
      itro!=ito->second.end();
      ++itro)
  {
    while(itr!=it->second.end() && itr->first<itro->first)
      ++itr;
    if(itr==it->second.end() || itro->first<itr->first)
    {
      it->second.insert(*itro);
      more=true;
    }
    else if(itr!=it->second.end())
    {
      assert(itr->first==itro->first);

      for(rangest::const_iterator itrro=itro->second.begin();
          itrro!=itro->second.end();
          ++itrro)
        more=gen(itr->second, itrro->first, itrro->second) ||
          more;

      ++itr;
    }
  }
#else
  values_innert::iterator itr=dest.begin();
  for(values_innert::const_iterator
      itro=other.begin();
      itro!=other.end();
      ++itro)
  {
    while(itr!=dest.end() && *itr<*itro)
      ++itr;
    if(itr==dest.end() || *itro<*itr)
    {
      dest.insert(itr, *itro);
      more=true;
    }
    else if(itr!=dest.end())
    {
      assert(*itr==*itro);
      ++itr;
    }
=======
>>>>>>> 2442e8f... Changed reaching-definitions data structure for reduced lookup times
  }
#endif

  return more;
}

/*******************************************************************\

Function: rd_range_domaint::merge

  Inputs:

 Outputs: returns true iff there is s.th. new

 Purpose:

\*******************************************************************/

bool rd_range_domaint::merge(
  const rd_range_domaint &other,
  locationt from,
  locationt to)
{
  bool more=false;

  valuest::iterator it=values.begin();
  for(valuest::const_iterator ito=other.values.begin();
      ito!=other.values.end();
      ++ito)
  {
#if 0
    while(it!=values.end() && it->first<ito->first)
      ++it;
    if(it==values.end() || ito->first<it->first)
    {
      values.insert(*ito);
      more=true;
    }
    else if(it!=values.end())
    {
      assert(it->first==ito->first);

<<<<<<< HEAD
      if(merge_inner(it->second, ito->second))
      {
        more=true;
        export_cache.erase(it->first);
=======
      ranges_at_loct::iterator itr=it->second.begin();
      for(ranges_at_loct::const_iterator itro=ito->second.begin();
          itro!=ito->second.end();
          ++itro)
      {
        while(itr!=it->second.end() && itr->first<itro->first)
          ++itr;
        if(itr==it->second.end() || itro->first<itr->first)
        {
          it->second.insert(*itro);
          more=true;
        }
        else if(itr!=it->second.end())
        {
          assert(itr->first==itro->first);

          for(rangest::const_iterator itrro=itro->second.begin();
              itrro!=itro->second.end();
              ++itrro)
            more=gen(itr->second, itrro->first, itrro->second) ||
                 more;

          ++itr;
        }
>>>>>>> 2442e8f... Changed reaching-definitions data structure for reduced lookup times
      }

      ++it;
    }
#else
    while(it!=values.end() && *it<*ito)
      ++it;
    if(it==values.end() || *ito<*it)
    {
      values.insert(*ito);
      more=true;
    }
    else if(it!=values.end())
    {
      assert(*it==*ito);
      ++it;
    }
#endif
  }

  export_cache_available=export_cache_available && !more;

  if(my_loc->location_number==12)
  {
    //std::cerr << "After merge" << std::endl;
    /*assert(from->location_number!=115 ||
           to->location_number!=12);*/
    //std::cerr << "from=" << from->location_number << std::endl;
    //std::cerr << "to=" << to->location_number << std::endl;
    //output(std::cerr);
  }

  return more;
}

/*******************************************************************\

<<<<<<< HEAD
Function: rd_range_domaint::merge_shared
=======
Function: reaching_definitions_analysist::~reaching_definitions_analysist

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

reaching_definitions_analysist::~reaching_definitions_analysist()
{
  if(may_alias) delete may_alias;
}

/*******************************************************************\

Function: reaching_definitions_analysist::initialize
>>>>>>> 58b702d... Changed --show-local-may-alias to use may_aliast, reach-def uses may_aliast

  Inputs:

 Outputs: returns true iff there is s.th. new

 Purpose:

\*******************************************************************/

bool rd_range_domaint::merge_shared(
  const rd_range_domaint &other,
  goto_programt::const_targett from,
  goto_programt::const_targett to,
  const namespacet &ns)
{
<<<<<<< HEAD
<<<<<<< HEAD
<<<<<<< HEAD
  // TODO: dirty vars
#if 0
  reaching_definitions_analysist *rd=
    dynamic_cast<reaching_definitions_analysist*>(&ai);
  assert(rd!=0);
#endif

=======
  assert(false);
=======
>>>>>>> 1acd251... merge_shared should be ok
  // TODO: consider dirty vars?
>>>>>>> 62f8b91... debugging reachdef
  bool more=false;

  valuest::iterator it=values.begin();
  for(valuest::const_iterator ito=other.values.begin();
      ito!=other.values.end();
      ++ito)
  {
<<<<<<< HEAD
<<<<<<< HEAD
    const irep_idt &identifier=ito->first;

    if(!ns.lookup(identifier).is_shared() /*&&
       !rd.get_is_dirty()(identifier)*/)
      continue;

=======
#if 0
>>>>>>> 9de64cc... Implement reaching definitions as sparse bitvector analysis for memory efficiency
    while(it!=values.end() && it->first<ito->first)
      ++it;
    if(it==values.end() || ito->first<it->first)
    {
      values.insert(*ito);
      more=true;
    }
    else if(it!=values.end())
    {
      assert(it->first==ito->first);

<<<<<<< HEAD
      if(merge_inner(it->second, ito->second))
      {
        more=true;
        export_cache.erase(it->first);
=======
      ranges_at_loct::iterator itr=it->second.begin();
      for(ranges_at_loct::const_iterator itro=ito->second.begin();
          itro!=ito->second.end();
          ++itro)
      {
        while(itr!=it->second.end() && itr->first<itro->first)
          ++itr;
        if(itr==it->second.end() || itro->first<itr->first)
        {
          it->second.insert(*itro);
          more=true;
        }
        else if(itr!=it->second.end())
        {
          assert(itr->first==itro->first);

          for(rangest::const_iterator itrro=itro->second.begin();
              itrro!=itro->second.end();
              ++itrro)
            more=gen(itr->second, itrro->first, itrro->second) ||
                 more;

          ++itr;
        }
>>>>>>> 2442e8f... Changed reaching-definitions data structure for reduced lookup times
      }

      ++it;
    }
<<<<<<< HEAD
=======
    local_may_aliases.insert(
      std::make_pair(f_it->first, local_may_aliast(f_it->second, ns)));
>>>>>>> 1ed43de... Preparing local_may_aliast for extensibility
=======
#else
    while(it!=values.end() && *it<*ito)
      ++it;
    if(it==values.end() || *ito<*it)
    {
      if(ns.lookup(bv_container->get(*ito).identifier).is_shared())
      {
        values.insert(*ito);
        more=true;
      }
    }
    else if(it!=values.end() &&
            !ns.lookup(bv_container->get(*ito).identifier).is_shared())
    {
      ++it;
    }
    else if(it!=values.end())
    {
      assert(*it==*ito);
      ++it;
    }
#endif
>>>>>>> 9de64cc... Implement reaching definitions as sparse bitvector analysis for memory efficiency
  }
=======
  may_alias=new may_aliast(goto_functions, ns);
>>>>>>> 58b702d... Changed --show-local-may-alias to use may_aliast, reach-def uses may_aliast

  export_cache_available=export_cache_available && !more;

  return more;
}

/*******************************************************************\

Function: rd_range_domaint::get

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

const rd_range_domaint::ranges_at_loct& rd_range_domaint::get(
  const irep_idt &identifier) const
{
<<<<<<< HEAD
<<<<<<< HEAD
  populate_cache(identifier);
=======
=======
  populate_cache();

>>>>>>> 9de64cc... Implement reaching definitions as sparse bitvector analysis for memory efficiency
  static ranges_at_loct empty;
>>>>>>> 2442e8f... Changed reaching-definitions data structure for reduced lookup times

<<<<<<< HEAD
  static ranges_at_loct empty;

<<<<<<< HEAD
  export_cachet::const_iterator entry=export_cache.find(identifier);

=======
  export_cachet::const_iterator entry=export_cache.find(identifier);

>>>>>>> 9de64cc... Implement reaching definitions as sparse bitvector analysis for memory efficiency
  if(entry==export_cache.end())
    return empty;
  else
    return entry->second;
=======
  state_map[l].set_may_alias(may_alias);
>>>>>>> 58b702d... Changed --show-local-may-alias to use may_aliast, reach-def uses may_aliast
}

/*******************************************************************\

Function: reaching_definitions_analysist::~reaching_definitions_analysist

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

reaching_definitions_analysist::~reaching_definitions_analysist()
{
  if(is_dirty) delete is_dirty;
  if(is_threaded) delete is_threaded;
  if(rd_dereference) delete rd_dereference;
}

/*******************************************************************\

Function: reaching_definitions_analysist::initialize

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void reaching_definitions_analysist::initialize(
  const goto_functionst &goto_functions)
{
  rd_dereference=new rd_dereferencet(ns, goto_functions, *this);

  is_threaded=new is_threadedt(goto_functions);

  is_dirty=new dirtyt(goto_functions);

  concurrency_aware_ait<rd_range_domaint>::initialize(goto_functions);
}
