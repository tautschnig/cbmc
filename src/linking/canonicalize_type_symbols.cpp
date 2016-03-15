/*******************************************************************\

Module: Replace all type symbols identifier by canical names

Author: Michael Tautschnig

\*******************************************************************/

#include <util/message.h>
#include <util/symbol_table.h>
#include <util/rename_symbol.h>
#include <util/namespace.h>

#include <langapi/language_util.h>

#include "canonicalize_type_symbols.h"

/*******************************************************************\

Function: rename_type_symbols

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

static void rename_type_symbols(
  const symbol_tablet &symbol_table,
  rename_symbolt &rename,
  message_handlert &message_handler)
{
  typedef hash_map_cont<irep_idt, irep_idt, irep_id_hash> id_mapt;
  id_mapt reverse_map;
  messaget message(message_handler);

  const namespacet ns(symbol_table);

  forall_symbols(s_it, symbol_table.symbols)
  {
    const symbolt &symbol=s_it->second;
    assert(s_it->first==symbol.name);

    if(!symbol.is_type)
      continue;

    const irep_idt &name=symbol.name;

    irep_idt id=type_to_name(ns, name, symbol.type);
    rename.insert_type(name, id);

    std::pair<id_mapt::const_iterator, bool> r_entry=
      reverse_map.insert(std::make_pair(id, name));
    if(!r_entry.second)
    {
      const irep_idt &prev_name=r_entry.first->second;

      message.debug() << "Types "
                      << from_type(ns, name, symbol.type)
                      << " and "
                      << from_type(ns, prev_name, ns.lookup(prev_name).type)
                      << messaget::eom;
    }
  }
}

/*******************************************************************\

Function: canonicalize_type_symbols

  Inputs: symbol table

 Outputs:

 Purpose:

\*******************************************************************/

void canonicalize_type_symbols(
  symbol_tablet &symbol_table,
  message_handlert &message_handler)
{
  rename_symbolt rename_types;
  rename_type_symbols(symbol_table, rename_types, message_handler);

  symbol_tablet new_symbol_table;

  forall_symbols(s_it, symbol_table.symbols)
  {
    // create a copy
    symbolt symbol=s_it->second;

    // apply the renaming
    rename_types(symbol.type);
    rename_types(symbol.value);

    rename_symbolt::type_mapt::const_iterator entry=
      rename_types.type_map.find(symbol.name);
    if(entry!=rename_types.type_map.end())
      symbol.name=entry->second;

    // there could be duplicates, which the debug message above
    // warns about
    new_symbol_table.add(symbol);
  }

  symbol_table.swap(new_symbol_table);
}

