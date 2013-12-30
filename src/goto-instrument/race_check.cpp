/*******************************************************************\

Module: Race Detection for Threaded Goto Programs

Author: Daniel Kroening

Date: February 2006

\*******************************************************************/

#include <util/hash_cont.h>
#include <util/std_expr.h>
#include <util/symbol_table.h>
#include <util/prefix.h>
#include <util/cprover_prefix.h>

#include <goto-programs/goto_program.h>
#include <goto-programs/goto_functions.h>

#include <pointer-analysis/value_sets.h>
#include <goto-programs/remove_skip.h>
#include <analyses/goto_rw.h>

#include "race_check.h"

#ifdef LOCAL_MAY
#include <analyses/local_may_alias.h>
#endif

class w_guardst
{
public:
  w_guardst(symbol_tablet &_symbol_table):symbol_table(_symbol_table)
  {
  }
  
  std::list<irep_idt> w_guards;

  const symbolt &get_guard_symbol(const irep_idt &object);
  
  const exprt get_guard_symbol_expr(const irep_idt &object)
  {
    return get_guard_symbol(object).symbol_expr();
  }
  
  const exprt get_w_guard_expr(const irep_idt& identifier)
  {
    return get_guard_symbol_expr(identifier);
  }
  
  const exprt get_assertion(const irep_idt& identifier)
  {
    return not_exprt(get_guard_symbol_expr(identifier));
  }
  
  void add_initialization(goto_programt &goto_program) const;
  
protected:
  symbol_tablet &symbol_table;
};

/*******************************************************************\

Function: w_guardst::get_guard_symbol

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

const symbolt &w_guardst::get_guard_symbol(const irep_idt &object)
{
  const irep_idt identifier=id2string(object)+"$w_guard";

  const symbol_tablet::symbolst::const_iterator it=
    symbol_table.symbols.find(identifier);

  if(it!=symbol_table.symbols.end())
    return it->second;
    
  w_guards.push_back(identifier);

  symbolt new_symbol;
  new_symbol.name=identifier;
  new_symbol.base_name=identifier;
  new_symbol.type=bool_typet();
  new_symbol.is_static_lifetime=true;
  new_symbol.value=false_exprt();
  
  symbolt *symbol_ptr;
  symbol_table.move(new_symbol, symbol_ptr);
  return *symbol_ptr;
}

/*******************************************************************\

Function: w_guardst::add_initialization

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void w_guardst::add_initialization(goto_programt &goto_program) const
{
  goto_programt::targett t=goto_program.instructions.begin();
  const namespacet ns(symbol_table);

  for(std::list<irep_idt>::const_iterator
      it=w_guards.begin();
      it!=w_guards.end();
      it++)
  {
    exprt symbol=ns.lookup(*it).symbol_expr();
  
    t=goto_program.insert_before(t);
    t->type=ASSIGN;
    t->code=code_assignt(symbol, false_exprt());
    
    t++;
  }
}

/*******************************************************************\

Function: comment

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::string comment(const irep_idt &identifier, bool write)
{
  std::string result;
  if(write)
    result="W/W";
  else
    result="R/W";

  result+=" data race on ";
  result+=id2string(identifier);
  return result;
}

/*******************************************************************\

Function: is_shared

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool is_shared(
  const namespacet &ns,
  const irep_idt &identifier)
{
  if(identifier==CPROVER_PREFIX "alloc" ||
     identifier==CPROVER_PREFIX "alloc_size" ||
     identifier=="stdin" ||
     identifier=="stdout" ||
     identifier=="stderr" ||
     identifier=="sys_nerr" ||
     has_prefix(id2string(identifier), "symex::invalid_object") ||
     has_prefix(id2string(identifier), "symex_dynamic::dynamic_object"))
    return false; // no race check

  const symbolt &symbol=ns.lookup(identifier);
  return symbol.is_shared();
}

/*******************************************************************\

Function: race_check

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool has_shared_entries(
  const namespacet &ns,
  const rw_range_sett &rw_set)
{
  forall_rw_range_set_r_objects(it, rw_set)
    if(is_shared(ns, it->first))
      return true;

  forall_rw_range_set_w_objects(it, rw_set)
    if(is_shared(ns, it->first))
      return true;

  return false;
}

/*******************************************************************\

Function: race_check

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void race_check(
  value_setst &value_sets,
  symbol_tablet &symbol_table,
#ifdef LOCAL_MAY
  const goto_functionst::goto_functiont& goto_function,
#endif
  goto_programt &goto_program,
  w_guardst &w_guards)
{
  namespacet ns(symbol_table);

#ifdef LOCAL_MAY
  local_may_aliast local_may(goto_function);
#endif

  Forall_goto_program_instructions(i_it, goto_program)
  {
    goto_programt::instructiont &instruction=*i_it;
    
    if(instruction.is_assign())
    {
      rw_guarded_range_set_value_sett rw_set(ns, value_sets);
      goto_rw(i_it, rw_set);
      
      if(!has_shared_entries(ns, rw_set))
        continue;
      
      goto_programt::instructiont original_instruction;
      original_instruction.swap(instruction);
      
      instruction.make_skip();
      i_it++;

      // now add assignments for what is written -- set
      forall_rw_range_set_w_objects(e_it, rw_set)
      {      
        if(!is_shared(ns, e_it->first)) continue;

        const guarded_range_domaint &guards=rw_set.get_ranges(e_it);
        exprt::operandst ops;
        ops.reserve(guards.size());
        for(guarded_range_domaint::const_iterator it=guards.begin();
            it!=guards.end();
            ++it)
          ops.push_back(it->second.second);
        exprt guard(disjunction(ops));
        
        goto_programt::targett t=goto_program.insert_before(i_it);

        t->type=ASSIGN;
        t->code=code_assignt(
          w_guards.get_w_guard_expr(e_it->first),
          guard);

        t->source_location=original_instruction.source_location;
        i_it=++t;
      }

      // insert original statement here
      {
        goto_programt::targett t=goto_program.insert_before(i_it);
        *t=original_instruction;
        i_it=++t;
      }

      // now add assignments for what is written -- reset
      forall_rw_range_set_w_objects(e_it, rw_set)
      {      
        if(!is_shared(ns, e_it->first)) continue;

        goto_programt::targett t=goto_program.insert_before(i_it);

        t->type=ASSIGN;
        t->code=code_assignt(
          w_guards.get_w_guard_expr(e_it->first),
          false_exprt());

        t->source_location=original_instruction.source_location;
        i_it=++t;
      }

      // now add assertions for what is read and written
      forall_rw_range_set_r_objects(e_it, rw_set)
      {
        if(!is_shared(ns, e_it->first)) continue;

        goto_programt::targett t=goto_program.insert_before(i_it);

        t->make_assertion(w_guards.get_assertion(e_it->first));
        t->source_location=original_instruction.source_location;
        t->source_location.set_comment(comment(e_it->first, false));
        i_it=++t;
      }

      forall_rw_range_set_w_objects(e_it, rw_set)
      {
        if(!is_shared(ns, e_it->first)) continue;

        goto_programt::targett t=goto_program.insert_before(i_it);

        t->make_assertion(w_guards.get_assertion(e_it->first));
        t->source_location=original_instruction.source_location;
        t->source_location.set_comment(comment(e_it->first, true));
        i_it=++t;
      }

      i_it--; // the for loop already counts us up      
    }
  }
  
  remove_skip(goto_program);  
}

/*******************************************************************\

Function: race_check

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void race_check(
  value_setst &value_sets,
  symbol_tablet &symbol_table,
#ifdef LOCAL_MAY
  const goto_functionst::goto_functiont& goto_function,
#endif
  goto_programt &goto_program)
{
  w_guardst w_guards(symbol_table);

  race_check(value_sets, symbol_table, 
#ifdef LOCAL_MAY
    goto_function, 
#endif
    goto_program, w_guards);

  w_guards.add_initialization(goto_program);
  goto_program.update();
}

/*******************************************************************\

Function: race_check

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void race_check(
  value_setst &value_sets,
  symbol_tablet &symbol_table,
  goto_functionst &goto_functions)
{
  w_guardst w_guards(symbol_table);

  Forall_goto_functions(f_it, goto_functions)
    if(f_it->first!=goto_functionst::entry_point() &&
       f_it->first!=CPROVER_PREFIX "initialize")
      race_check(value_sets, symbol_table, 
#ifdef LOCAL_MAY
        f_it->second, 
#endif
        f_it->second.body, w_guards);

  // get "main"
  goto_functionst::function_mapt::iterator
    m_it=goto_functions.function_map.find(goto_functions.entry_point());

  if(m_it==goto_functions.function_map.end())
    throw "Race check instrumentation needs an entry point";

  goto_programt &main=m_it->second.body;
  w_guards.add_initialization(main);
  goto_functions.update();
}
