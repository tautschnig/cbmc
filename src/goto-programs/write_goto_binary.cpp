/*******************************************************************\

Module: Write GOTO binaries

Author: CM Wintersteiger

\*******************************************************************/

/// \file
/// Write GOTO binaries

#include "write_goto_binary.h"

#include <fstream>
#include <iostream>
#include <cstdlib>

#include <util/exception_utils.h>
#include <util/invariant.h>
#include <util/irep_serialization.h>
#include <util/message.h>
#include <util/symbol_table.h>

#include <goto-programs/goto_model.h>

/// Writes a goto program to disc, using goto binary format ver 4
bool write_goto_binary_v4(
  std::ostream &out,
  const symbol_tablet &symbol_table,
  const goto_functionst &goto_functions,
  irep_serializationt &irepconverter)
{
  bool dbg = getenv("IREP_DEBUG") != nullptr;
  // first write symbol table

  irepconverter.write_gb_word(out, symbol_table.symbols.size());

  for(const auto &symbol_pair : symbol_table.symbols)
  {
    // Since version 2, symbols are not converted to ireps,
    // instead they are saved in a custom binary format

    const symbolt &sym = symbol_pair.second;

    if(dbg)
      std::cout << "C: sym.type" << std::endl;
    irepconverter.reference_convert(sym.type, out);
    if(dbg)
      std::cout << "C: sym.value" << std::endl;
    irepconverter.reference_convert(sym.value, out);
    if(dbg)
      std::cout << "C: sym.location" << std::endl;
    irepconverter.reference_convert(sym.location, out);

    if(dbg)
      std::cout << "C: sym.name" << std::endl;
    irepconverter.write_string_ref(out, sym.name);
    if(dbg)
      std::cout << "C: sym.module" << std::endl;
    irepconverter.write_string_ref(out, sym.module);
    if(dbg)
      std::cout << "C: sym.base_name" << std::endl;
    irepconverter.write_string_ref(out, sym.base_name);
    if(dbg)
      std::cout << "C: sym.mode" << std::endl;
    irepconverter.write_string_ref(out, sym.mode);
    if(dbg)
      std::cout << "C: sym.pretty_name" << std::endl;
    irepconverter.write_string_ref(out, sym.pretty_name);

    if(dbg)
      std::cout << "C: T" << std::endl;
    irepconverter.write_gb_word(out, 0); // old: sym.ordering

    unsigned flags=0;
    flags = (flags << 1) | static_cast<int>(sym.is_weak);
    flags = (flags << 1) | static_cast<int>(sym.is_type);
    flags = (flags << 1) | static_cast<int>(sym.is_property);
    flags = (flags << 1) | static_cast<int>(sym.is_macro);
    flags = (flags << 1) | static_cast<int>(sym.is_exported);
    flags = (flags << 1) | static_cast<int>(sym.is_input);
    flags = (flags << 1) | static_cast<int>(sym.is_output);
    flags = (flags << 1) | static_cast<int>(sym.is_state_var);
    flags = (flags << 1) | static_cast<int>(sym.is_parameter);
    flags = (flags << 1) | static_cast<int>(sym.is_auxiliary);
    flags = (flags << 1) | static_cast<int>(false); // sym.binding;
    flags = (flags << 1) | static_cast<int>(sym.is_lvalue);
    flags = (flags << 1) | static_cast<int>(sym.is_static_lifetime);
    flags = (flags << 1) | static_cast<int>(sym.is_thread_local);
    flags = (flags << 1) | static_cast<int>(sym.is_file_local);
    flags = (flags << 1) | static_cast<int>(sym.is_extern);
    flags = (flags << 1) | static_cast<int>(sym.is_volatile);

    if(dbg)
      std::cout << "C: flags" << std::endl;
    irepconverter.write_gb_word(out, flags);
  }

  // now write functions, but only those with body

  unsigned cnt=0;
  forall_goto_functions(it, goto_functions)
    if(it->second.body_available())
      cnt++;

  irepconverter.write_gb_word(out, cnt);

  for(const auto &fct : goto_functions.function_map)
  {
    if(fct.second.body_available())
    {
      // Since version 2, goto functions are not converted to ireps,
      // instead they are saved in a custom binary format

      write_gb_string(out, id2string(fct.first)); // name
      irepconverter.write_gb_word(out, fct.second.body.instructions.size()); // # instructions

      forall_goto_program_instructions(i_it, fct.second.body)
      {
        const goto_programt::instructiont &instruction = *i_it;

        irepconverter.reference_convert(instruction.code, out);
        irepconverter.write_string_ref(out, instruction.function);
        irepconverter.reference_convert(instruction.source_location, out);
        irepconverter.write_gb_word(out, (long)instruction.type);
        irepconverter.reference_convert(instruction.guard, out);
        irepconverter.write_string_ref(out, irep_idt()); // former event
        irepconverter.write_gb_word(out, instruction.target_number);

        irepconverter.write_gb_word(out, instruction.targets.size());

        for(const auto &t_it : instruction.targets)
          irepconverter.write_gb_word(out, t_it->target_number);

        irepconverter.write_gb_word(out, instruction.labels.size());

        for(const auto &l_it : instruction.labels)
          irepconverter.write_string_ref(out, l_it);
      }
    }
  }

  // irepconverter.output_map(f);
  // irepconverter.output_string_map(f);

  return false;
}

/// Writes a goto program to disc
bool write_goto_binary(
  std::ostream &out,
  const goto_modelt &goto_model,
  int version)
{
  return write_goto_binary(
    out,
    goto_model.symbol_table,
    goto_model.goto_functions,
    version);
}

/// Writes a goto program to disc
bool write_goto_binary(
  std::ostream &out,
  const symbol_tablet &symbol_table,
  const goto_functionst &goto_functions,
  int version)
{
  irep_serializationt::ireps_containert irepc;
  irep_serializationt irepconverter(irepc);

  // header
  out << char(0x7f) << "GBF";
  irepconverter.write_gb_word(out, version);

  const int current_goto_version = 4;
  if(version < current_goto_version)
    throw invalid_command_line_argument_exceptiont(
      "version " + std::to_string(version) + " no longer supported",
      "supported version = " + std::to_string(current_goto_version));
  else if(version > current_goto_version)
    throw invalid_command_line_argument_exceptiont(
      "unknown goto binary version " + std::to_string(version),
      "supported version = " + std::to_string(current_goto_version));
  else
    return write_goto_binary_v4(
      out, symbol_table, goto_functions, irepconverter);
}

/// Writes a goto program to disc
bool write_goto_binary(
  const std::string &filename,
  const goto_modelt &goto_model,
  message_handlert &message_handler)
{
  std::ofstream out(filename, std::ios::binary);

  if(!out)
  {
    messaget message(message_handler);
    message.error() <<
      "Failed to open `" << filename << "'";
    return true;
  }

  return write_goto_binary(out, goto_model);
}
