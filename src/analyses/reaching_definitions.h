/*******************************************************************\

Module: Range-based reaching definitions analysis (following Field-
        Sensitive Program Dependence Analysis, Litvak et al., FSE 2010)

Author: Michael Tautschnig

Date: February 2013

\*******************************************************************/

<<<<<<< HEAD
#ifndef CPROVER_REACHING_DEFINITIONS_H
#define CPROVER_REACHING_DEFINITIONS_H

#include "ai.h"
#include "goto_rw.h"
=======
#include "static_analysis.h"

class if_exprt;
class byte_extract_exprt;
class dereference_exprt;
class may_aliast;
>>>>>>> 58b702d... Changed --show-local-may-alias to use may_aliast, reach-def uses may_aliast

class rd_dereferencet;
class is_threadedt;
class dirtyt;
class reaching_definitions_analysist;

// requirement: V has a member "identifier" of type irep_idt
template<typename V>
class sparse_bitvector_analysist
{
public:
  inline const V& get(const std::size_t value_index) const
  {
    assert(value_index<values.size());
    return values[value_index]->first;
  }

  inline std::size_t add(const V& value)
  {
    inner_mapt &m=value_map[value.identifier];

    std::pair<typename inner_mapt::iterator, bool> entry=
      m.insert(std::make_pair(value, values.size()));

    if(entry.second)
      values.push_back(entry.first);

    return entry.first->second;
  }

protected:
  typedef typename std::map<V, std::size_t> inner_mapt;
  std::vector<typename inner_mapt::const_iterator> values;
  hash_map_cont<irep_idt, inner_mapt, irep_id_hash> value_map;
};

struct reaching_definitiont
{
  irep_idt identifier;
  ai_domain_baset::locationt definition_at;
  range_spect bit_begin;
  range_spect bit_end;
};

inline bool operator<(
  const reaching_definitiont &a,
  const reaching_definitiont &b)
{
  if(a.definition_at<b.definition_at) return true;
  if(b.definition_at<a.definition_at) return false;

  if(a.bit_begin<b.bit_begin) return true;
  if(b.bit_begin<a.bit_begin) return false;

  if(a.bit_end<b.bit_end) return true;
  if(b.bit_end<a.bit_end) return false;

  // we do not expect comparison of unrelated definitions
  // as this operator< is only used in sparse_bitvector_analysist
  assert(a.identifier==b.identifier);

  return false;
}

class rd_range_domaint:public ai_domain_baset
{
public:
  rd_range_domaint():
<<<<<<< HEAD
    ai_domain_baset(),
    bv_container(0)
=======
    may_alias(0)
>>>>>>> 58b702d... Changed --show-local-may-alias to use may_aliast, reach-def uses may_aliast
  {
  }

  inline void set_bitvector_container(
    sparse_bitvector_analysist<reaching_definitiont> &_bv_container)
  {
    bv_container=&_bv_container;
  }

  virtual void transform(
      locationt from,
      locationt to,
      ai_baset &ai,
      const namespacet &ns);

  virtual void output(
      std::ostream &out,
      const ai_baset &ai,
      const namespacet &ns) const
  {
    output(out);
  }

  // returns true iff there is s.th. new
  bool merge(
    const rd_range_domaint &other,
    locationt from,
    locationt to);
  bool merge_shared(
    const rd_range_domaint &other,
    locationt from,
    locationt to,
    const namespacet &ns);

<<<<<<< HEAD
<<<<<<< HEAD
  // each element x represents a range of bits [x.first, x.second)
  typedef std::multimap<range_spect, range_spect> rangest;
  typedef std::map<locationt, rangest> ranges_at_loct;

  const ranges_at_loct& get(const irep_idt &identifier) const;
  inline const void clear_cache(const irep_idt &identifier) const
  {
    export_cache[identifier].clear();
=======
  void set_may_alias(may_aliast *a)
  {
    may_alias=a;
>>>>>>> 58b702d... Changed --show-local-may-alias to use may_aliast, reach-def uses may_aliast
  }

protected:
  sparse_bitvector_analysist<reaching_definitiont> *bv_container;

  typedef std::set<std::size_t> values_innert;
  #ifdef USE_DSTRING
  typedef std::map<irep_idt, values_innert> valuest;
  #else
  typedef hash_map_cont<irep_idt, values_innert, irep_id_hash> valuest;
=======
  // each element x represents a range of bits [x.first, x.second)
  typedef std::list<std::pair<range_spect, range_spect> > rangest;
  typedef std::map<locationt, rangest> ranges_at_loct;
  #ifdef USE_DSTRING
  typedef std::map<irep_idt, ranges_at_loct> valuest;
  #else
  typedef hash_map_cont<irep_idt, ranges_at_loct, irep_id_hash> valuest;
>>>>>>> 2442e8f... Changed reaching-definitions data structure for reduced lookup times
  #endif
  valuest values;

<<<<<<< HEAD
<<<<<<< HEAD
  #ifdef USE_DSTRING
  typedef std::map<irep_idt, ranges_at_loct> export_cachet;
  #else
  typedef hash_map_cont<irep_idt, ranges_at_loct, irep_id_hash>
    export_cachet;
  #endif
  mutable export_cachet export_cache;
=======
  const ranges_at_loct& get(const irep_idt &identifier) const;
>>>>>>> 2442e8f... Changed reaching-definitions data structure for reduced lookup times

  void populate_cache(const irep_idt &identifier) const;
=======
  may_aliast * may_alias;
>>>>>>> 58b702d... Changed --show-local-may-alias to use may_aliast, reach-def uses may_aliast

  void transform_dead(
    const namespacet &ns,
    locationt from);
  void transform_start_thread(
    const namespacet &ns,
    reaching_definitions_analysist &rd);
  void transform_function_call(
    const namespacet &ns,
    locationt from,
    locationt to,
    reaching_definitions_analysist &rd);
  void transform_end_function(
    const namespacet &ns,
    locationt from,
    locationt to,
    reaching_definitions_analysist &rd);
  void transform_assign(
    const namespacet &ns,
    locationt from,
    locationt to,
    reaching_definitions_analysist &rd);

  void kill(
    const irep_idt &identifier,
    const range_spect &range_start,
    const range_spect &range_end);
  void kill_inf(
    const irep_idt &identifier,
    const range_spect &range_start);
  bool gen(
    locationt from,
    const irep_idt &identifier,
    const range_spect &range_start,
    const range_spect &range_end);
  bool gen(
    rangest &ranges,
  locationt from,
  const irep_idt &identifier,
    const range_spect &range_start,
    const range_spect &range_end);

  void output(std::ostream &out) const;

  bool merge_inner(
    values_innert &dest,
    const values_innert &other);
};

class reaching_definitions_analysist :
  public concurrency_aware_ait<rd_range_domaint>,
  public sparse_bitvector_analysist<reaching_definitiont>
{
public:
  // constructor
  explicit reaching_definitions_analysist(const namespacet &_ns):
<<<<<<< HEAD
    concurrency_aware_ait<rd_range_domaint>(),
    ns(_ns),
    rd_dereference(0),
    is_threaded(0),
    is_dirty(0)
=======
    static_analysist<rd_range_domaint>(_ns),
    may_alias(0)
>>>>>>> 58b702d... Changed --show-local-may-alias to use may_aliast, reach-def uses may_aliast
  {
  }

  virtual ~reaching_definitions_analysist();

  virtual void initialize(
    const goto_functionst &goto_functions);

  virtual statet &get_state(goto_programt::const_targett l)
  {
<<<<<<< HEAD
    statet &s=concurrency_aware_ait<rd_range_domaint>::get_state(l);

    rd_range_domaint *rd_state=dynamic_cast<rd_range_domaint*>(&s);
    assert(rd_state!=0);

    rd_state->set_bitvector_container(*this);

    return s;
=======
    throw "reaching definitions uses may_aliast, cannot be used on goto_programt";
>>>>>>> 58b702d... Changed --show-local-may-alias to use may_aliast, reach-def uses may_aliast
  }

  rd_dereferencet &get_rd_dereference() const
  {
    assert(rd_dereference);
    return *rd_dereference;
  }

<<<<<<< HEAD
  const is_threadedt &get_is_threaded() const
  {
    assert(is_threaded);
    return *is_threaded;
  }
=======
protected:
  may_aliast * may_alias;
>>>>>>> 58b702d... Changed --show-local-may-alias to use may_aliast, reach-def uses may_aliast

  const dirtyt &get_is_dirty() const
  {
    assert(is_dirty);
    return *is_dirty;
  }

protected:
  const namespacet &ns;
  rd_dereferencet * rd_dereference;
  is_threadedt * is_threaded;
  dirtyt * is_dirty;
};

#endif

