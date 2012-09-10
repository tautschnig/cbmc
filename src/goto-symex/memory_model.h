/*******************************************************************\

Module: Memory models for partial order concurrency

Author: Michael Tautschnig, michael.tautschnig@cs.ox.ac.uk

\*******************************************************************/

#ifndef CPROVER_MEMORY_MODEL_H
#define CPROVER_MEMORY_MODEL_H

#include "partial_order_concurrency.h"

class memory_model_baset
{
protected:
  typedef partial_order_concurrencyt::adj_matricest adj_matricest;
  typedef partial_order_concurrencyt::evtt evtt;

public:
  virtual ~memory_model_baset();

  virtual unsigned steps_per_event(
      const partial_order_concurrencyt &poc,
      evtt::event_dirt direction) const=0;

  virtual void add_sub_clock_rules(
      partial_order_concurrencyt &poc) const=0;
  virtual void add_atomic_sections(
      partial_order_concurrencyt &poc) const=0;
  virtual void add_program_order(
      partial_order_concurrencyt &poc,
      const numbered_evtst &thread,
      adj_matricest &po) const=0;
  virtual void add_read_from(
      partial_order_concurrencyt &poc,
      const partial_order_concurrencyt::per_valuet &reads,
      const partial_order_concurrencyt::per_valuet &writes,
      adj_matricest &rf) const=0;
  virtual void add_write_serialisation_internal(
      partial_order_concurrencyt &poc,
      const numbered_evtst &thread,
      adj_matricest &ws) const=0;
  virtual void add_write_serialisation_external(
      partial_order_concurrencyt &poc,
      const partial_order_concurrencyt::per_valuet &writes,
      adj_matricest &ws) const=0;
  virtual void add_from_read(
      partial_order_concurrencyt &poc,
      const adj_matricest &rf,
      const adj_matricest &ws,
      adj_matricest &fr) const=0;
  virtual void add_barriers(
      partial_order_concurrencyt &poc,
      const numbered_evtst &thread,
      const adj_matricest &rf,
      const adj_matricest &ws,
      const adj_matricest &fr,
      adj_matricest &ab) const=0;

protected:
  virtual bool uses_check(partial_order_concurrencyt::acyclict check) const=0;
  virtual bool po_is_relaxed(
      partial_order_concurrencyt &poc,
      const evtt &e1,
      const evtt &e2) const=0;
  virtual bool rf_is_relaxed(
      const evtt &w,
      const evtt &r,
      const bool is_rfi) const=0;

  void add_from_read(
      partial_order_concurrencyt &poc,
      const partial_order_concurrencyt::acyclict check,
      const partial_order_concurrencyt::adj_matrixt &rf,
      const partial_order_concurrencyt::adj_matrixt &ws,
      partial_order_concurrencyt::adj_matrixt &fr) const;
};

class memory_model_sct : public memory_model_baset
{
public:
  virtual ~memory_model_sct();

  virtual unsigned steps_per_event(
      const partial_order_concurrencyt &poc,
      evtt::event_dirt direction) const;

  virtual void add_sub_clock_rules(
      partial_order_concurrencyt &poc) const;
  virtual void add_atomic_sections(
      partial_order_concurrencyt &poc) const;
  virtual void add_program_order(
      partial_order_concurrencyt &poc,
      const numbered_evtst &thread,
      adj_matricest &po) const;
  virtual void add_read_from(
      partial_order_concurrencyt &poc,
      const partial_order_concurrencyt::per_valuet &reads,
      const partial_order_concurrencyt::per_valuet &writes,
      adj_matricest &rf) const;
  virtual void add_write_serialisation_internal(
      partial_order_concurrencyt &poc,
      const numbered_evtst &thread,
      adj_matricest &ws) const;
  virtual void add_write_serialisation_external(
      partial_order_concurrencyt &poc,
      const partial_order_concurrencyt::per_valuet &writes,
      adj_matricest &ws) const;
  virtual void add_from_read(
      partial_order_concurrencyt &poc,
      const adj_matricest &rf,
      const adj_matricest &ws,
      adj_matricest &fr) const;
  virtual void add_barriers(
      partial_order_concurrencyt &poc,
      const numbered_evtst &thread,
      const adj_matricest &rf,
      const adj_matricest &ws,
      const adj_matricest &fr,
      adj_matricest &ab) const;

protected:
  virtual bool uses_check(partial_order_concurrencyt::acyclict check) const;
  virtual bool po_is_relaxed(
      partial_order_concurrencyt &poc,
      const evtt &e1,
      const evtt &e2) const;
  virtual bool rf_is_relaxed(
      const evtt &w,
      const evtt &r,
      const bool is_rfi) const;
};

class memory_model_tsot : public memory_model_sct
{
public:
  virtual ~memory_model_tsot();

  virtual unsigned steps_per_event(
      const partial_order_concurrencyt &poc,
      evtt::event_dirt direction) const;

  virtual void add_barriers(
      partial_order_concurrencyt &poc,
      const numbered_evtst &thread,
      const adj_matricest &rf,
      const adj_matricest &ws,
      const adj_matricest &fr,
      adj_matricest &ab) const;

protected:
  virtual bool uses_check(partial_order_concurrencyt::acyclict check) const;
  virtual bool po_is_relaxed(
      partial_order_concurrencyt &poc,
      const evtt &e1,
      const evtt &e2) const;
  virtual bool rf_is_relaxed(
      const evtt &w,
      const evtt &r,
      const bool is_rfi) const;
};

class memory_model_psot : public memory_model_tsot
{
public:
  virtual ~memory_model_psot();

protected:
  virtual bool po_is_relaxed(
      partial_order_concurrencyt &poc,
      const evtt &e1,
      const evtt &e2) const;
};

class memory_model_alphat : public memory_model_tsot
{
public:
  virtual ~memory_model_alphat();

protected:
  virtual bool uses_check(partial_order_concurrencyt::acyclict check) const;
  virtual bool po_is_relaxed(
      partial_order_concurrencyt &poc,
      const evtt &e1,
      const evtt &e2) const;
};

class memory_model_rmot : public memory_model_alphat
{
public:
  virtual ~memory_model_rmot();

  virtual void add_program_order(
      partial_order_concurrencyt &poc,
      const numbered_evtst &thread,
      adj_matricest &po) const;

protected:
  virtual bool po_is_relaxed(
      partial_order_concurrencyt &poc,
      const evtt &e1,
      const evtt &e2) const;
};

class memory_model_powert : public memory_model_alphat
{
public:
  virtual ~memory_model_powert();

  virtual void add_barriers(
      partial_order_concurrencyt &poc,
      const numbered_evtst &thread,
      const adj_matricest &rf,
      const adj_matricest &ws,
      const adj_matricest &fr,
      adj_matricest &ab) const;

protected:
  virtual bool po_is_relaxed(
      partial_order_concurrencyt &poc,
      const evtt &e1,
      const evtt &e2) const;
  virtual bool rf_is_relaxed(
      const evtt &w,
      const evtt &r,
      const bool is_rfi) const;
};

class memory_model_armt : public memory_model_powert
{
public:
  virtual ~memory_model_armt();

  virtual void add_program_order(
      partial_order_concurrencyt &poc,
      const numbered_evtst &thread,
      adj_matricest &po) const;

protected:
  virtual bool po_is_relaxed(
      partial_order_concurrencyt &poc,
      const evtt &e1,
      const evtt &e2) const;
};

class memory_model_ppc_pldit : public memory_model_sct
{
public:
  virtual ~memory_model_ppc_pldit();

  virtual unsigned steps_per_event(
      const partial_order_concurrencyt &poc,
      evtt::event_dirt direction) const;

  virtual void add_sub_clock_rules(
      partial_order_concurrencyt &poc) const;
  virtual void add_program_order(
      partial_order_concurrencyt &poc,
      const numbered_evtst &thread,
      adj_matricest &po) const;
  virtual void add_read_from(
      partial_order_concurrencyt &poc,
      const partial_order_concurrencyt::per_valuet &reads,
      const partial_order_concurrencyt::per_valuet &writes,
      adj_matricest &rf) const;
  virtual void add_write_serialisation_external(
      partial_order_concurrencyt &poc,
      const partial_order_concurrencyt::per_valuet &writes,
      adj_matricest &ws) const;
  virtual void add_from_read(
      partial_order_concurrencyt &poc,
      const adj_matricest &rf,
      const adj_matricest &ws,
      adj_matricest &fr) const;
  virtual void add_barriers(
      partial_order_concurrencyt &poc,
      const numbered_evtst &thread,
      const adj_matricest &rf,
      const adj_matricest &ws,
      const adj_matricest &fr,
      adj_matricest &ab) const;

protected:
  virtual bool uses_check(partial_order_concurrencyt::acyclict check) const;
  virtual bool po_is_relaxed(
      partial_order_concurrencyt &poc,
      const evtt &e1,
      const evtt &e2) const;
  virtual bool rf_is_relaxed(
      const evtt &w,
      const evtt &r,
      const bool is_rfi) const;

  void add_from_read(
      partial_order_concurrencyt &poc,
      const partial_order_concurrencyt::acyclict check,
      const partial_order_concurrencyt::adj_matrixt &rf,
      const partial_order_concurrencyt::adj_matrixt &ws,
      partial_order_concurrencyt::adj_matrixt &fr) const;
};

class memory_model_arm_pldit : public memory_model_ppc_pldit
{
public:
  virtual ~memory_model_arm_pldit();

  virtual void add_program_order(
      partial_order_concurrencyt &poc,
      const numbered_evtst &thread,
      adj_matricest &po) const;

protected:
  virtual bool po_is_relaxed(
      partial_order_concurrencyt &poc,
      const evtt &e1,
      const evtt &e2) const;
};

#endif
