/*******************************************************************\

Module: Python front-end: exception propagation at call sites

\*******************************************************************/

/// \file
/// After goto conversion, a call that may raise is followed by a jump to
/// the end of its Python statement when an exception is active (PLR 8.4).

#include "python_exception_instrumentation.h"

#include <goto-programs/goto_model.h>

#include <map>

void instrument_python_call_exceptions(goto_modelt &goto_model)
{
  const symbolt *exc =
    goto_model.symbol_table.lookup("python::__exception_active");
  if(exc == nullptr)
    return;
  const symbol_exprt flag = exc->symbol_expr();
  bool changed = false;
  for(auto &[name, fn] : goto_model.goto_functions.function_map)
  {
    if(!fn.body_available())
      continue;
    goto_programt &body = fn.body;
    std::map<irep_idt, goto_programt::targett> ends;
    for(auto it = body.instructions.begin(); it != body.instructions.end();
        ++it)
    {
      const irep_idt &tag = it->source_location().get(ID_python_exc_stmt);
      if(it->is_skip() && !tag.empty())
        ends[tag] = it;
    }
    if(ends.empty())
      continue;
    for(auto it = body.instructions.begin(); it != body.instructions.end();
        ++it)
    {
      if(!it->is_function_call())
        continue;
      const irep_idt &tag = it->source_location().get(ID_python_exc_stmt);
      if(tag.empty())
        continue;
      auto e = ends.find(tag);
      if(e == ends.end())
        continue;
      body.insert_after(
        it, goto_programt::make_goto(e->second, flag, it->source_location()));
      changed = true;
    }
  }
  if(changed)
    goto_model.goto_functions.update();
}
