/*******************************************************************\

Module: COBOL Language Entry Point

Author: Kiro

\*******************************************************************/

/// \file
/// Builds __CPROVER_initialize and __CPROVER__start for a COBOL program.

#include "cobol_entry_point.h"

#include <util/config.h>
#include <util/message.h>
#include <util/prefix.h>
#include <util/std_code.h>
#include <util/std_types.h>
#include <util/symbol_table_base.h>

#include <goto-programs/goto_functions.h>

#include <linking/static_lifetime_init.h>

#include "cobol_language.h"

/// Find the COBOL program (main) function symbol. Honours config.main if set,
/// otherwise picks the single user-level COBOL function.
/// \param symbol_table: the symbol table
/// \return pointer to the main symbol, or nullptr if none/ambiguous
static const symbolt *find_main_program(const symbol_table_baset &symbol_table)
{
  const symbolt *result = nullptr;

  for(const auto &pair : symbol_table.symbols)
  {
    const symbolt &symbol = pair.second;
    if(symbol.mode != COBOL_MODE || symbol.type.id() != ID_code)
      continue;
    if(has_prefix(id2string(symbol.base_name), "__CPROVER"))
      continue;
    if(symbol.value.is_nil())
      continue;

    if(config.main.has_value())
    {
      if(id2string(symbol.base_name) == config.main.value())
        return &symbol;
      continue;
    }

    if(result != nullptr)
      return nullptr; // ambiguous: more than one candidate
    result = &symbol;
  }

  return result;
}

/// Create __CPROVER_initialize, assigning each static-lifetime symbol its
/// initial value.
static void generate_cobol_init_function(symbol_table_baset &symbol_table)
{
  symbolt init{INITIALIZE_FUNCTION, code_typet({}, empty_typet{}), COBOL_MODE};

  code_blockt dest;
  for(const auto &pair : symbol_table.symbols)
  {
    const symbolt &symbol = pair.second;
    if(
      symbol.is_static_lifetime && symbol.value.is_not_nil() &&
      symbol.mode == COBOL_MODE && symbol.type.id() != ID_code)
    {
      dest.add(code_frontend_assignt{symbol.symbol_expr(), symbol.value});
    }
  }
  init.value = std::move(dest);
  symbol_table.add(init);
}

bool cobol_entry_point(
  symbol_table_baset &symbol_table,
  message_handlert &message_handler)
{
  // Already present?
  if(
    symbol_table.symbols.find(goto_functionst::entry_point()) !=
    symbol_table.symbols.end())
    return false;

  const symbolt *main = find_main_program(symbol_table);
  if(main == nullptr)
  {
    messaget message(message_handler);
    message.error() << "COBOL: could not determine a unique PROGRAM-ID to use "
                       "as the entry point"
                    << messaget::eom;
    return true;
  }

  generate_cobol_init_function(symbol_table);

  code_blockt start_body;

  // Call __CPROVER_initialize.
  {
    const symbolt &init = symbol_table.lookup_ref(INITIALIZE_FUNCTION);
    code_function_callt call_init{init.symbol_expr()};
    call_init.add_source_location() = main->location;
    start_body.add(call_init);
  }

  // Call the main program.
  {
    code_function_callt call_main{main->symbol_expr()};
    call_main.add_source_location() = main->location;
    start_body.add(call_main);
  }

  symbolt start_symbol{
    goto_functionst::entry_point(), code_typet{{}, empty_typet{}}, COBOL_MODE};
  start_symbol.base_name = goto_functionst::entry_point();
  start_symbol.value = std::move(start_body);

  if(!symbol_table.insert(std::move(start_symbol)).second)
  {
    messaget message(message_handler);
    message.error() << "COBOL: failed to insert start symbol" << messaget::eom;
    return true;
  }

  return false;
}
