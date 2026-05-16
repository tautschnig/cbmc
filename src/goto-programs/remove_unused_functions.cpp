/*******************************************************************\

Module: Unused function removal

Author: CM Wintersteiger

\*******************************************************************/

/// \file
/// Unused function removal

#include "remove_unused_functions.h"

#include <util/message.h>
#include <util/std_expr.h>

#include "goto_model.h"

void remove_unused_functions(
  goto_modelt &goto_model,
  message_handlert &message_handler)
{
  remove_unused_functions(goto_model.goto_functions, message_handler);
}

void remove_unused_functions(
  goto_functionst &functions,
  message_handlert &message_handler)
{
  std::set<irep_idt> used_functions;
  std::list<goto_functionst::function_mapt::iterator> unused_functions;
  find_used_functions(
    goto_functionst::entry_point(), functions, used_functions);

  for(goto_functionst::function_mapt::iterator it=
        functions.function_map.begin();
      it!=functions.function_map.end();
      it++)
  {
    if(used_functions.find(it->first)==used_functions.end())
      unused_functions.push_back(it);
  }

  messaget message(message_handler);

  if(!unused_functions.empty())
  {
    message.statistics()
      << "Dropping " << unused_functions.size() << " of " <<
      functions.function_map.size() << " functions (" <<
      used_functions.size() << " used)" << messaget::eom;
  }

  for(const auto &f : unused_functions)
    functions.function_map.erase(f);
}

void find_used_functions(
  const irep_idt &start,
  goto_functionst &functions,
  std::set<irep_idt> &seen)
{
  std::pair<std::set<irep_idt>::const_iterator, bool> res =
    seen.insert(start);

  if(!res.second)
    return;
  else
  {
    goto_functionst::function_mapt::const_iterator f_it =
      functions.function_map.find(start);

    if(f_it!=functions.function_map.end())
    {
      // Helper: recursively collect identifiers of any
      // `address_of(symbol)` where the symbol has code type. These
      // functions are "used" via address-taken (e.g., DFCC's
      // `__dfcc_instrumented_functions[pointer_object(address_of(free))]`
      // populates the instrumented-functions map by identity, even
      // though it never calls free directly). Without this pin the
      // validator's `every function whose address is taken must be
      // in the function map` check fires after we drop free.
      std::function<void(const exprt &)> visit_addr_of_funs =
        [&](const exprt &e)
      {
        if(e.id() == ID_address_of && e.operands().size() == 1)
        {
          const exprt &pointee = e.operands()[0];
          if(pointee.id() == ID_symbol && pointee.type().id() == ID_code)
            find_used_functions(
              to_symbol_expr(pointee).get_identifier(), functions, seen);
        }
        for(const auto &op : e.operands())
          visit_addr_of_funs(op);
      };

      for(const auto &instruction : f_it->second.body.instructions)
      {
        if(instruction.is_function_call())
        {
          const auto &function = instruction.call_function();

          const irep_idt &identifier = to_symbol_expr(function).identifier();

          find_used_functions(identifier, functions, seen);
        }
        // Visit every operand of every non-CALL instruction for
        // address-of-function uses too.
        instruction.apply([&](const exprt &e) { visit_addr_of_funs(e); });
      }
    }
  }
}
