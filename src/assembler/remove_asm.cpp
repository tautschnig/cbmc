/*******************************************************************\

Module: Remove 'asm' statements by compiling them into suitable
        standard goto program instructions

Author: Daniel Kroening

Date:   December 2014

\*******************************************************************/

/// \file
/// Remove 'asm' statements by compiling them into suitable standard goto
/// program instructions

#include "remove_asm.h"

#include <util/c_types.h>
#include <util/config.h>
#include <util/message.h>
#include <util/pointer_expr.h>
#include <util/prefix.h>
#include <util/range.h>
#include <util/std_code.h>
#include <util/string_constant.h>

#include <goto-programs/goto_model.h>
#include <goto-programs/remove_skip.h>

#include "asm_approximation.h"
#include "asm_semantics.h"
#include "assembler_parser.h"

#include <cctype>
#include <set>

class remove_asmt
{
public:
  remove_asmt(
    symbol_tablet &_symbol_table,
    goto_functionst &_goto_functions,
    message_handlert &message_handler)
    : symbol_table(_symbol_table),
      goto_functions(_goto_functions),
      message_handler(message_handler),
      log(message_handler),
      semantics(make_asm_semantics(config.ansi_c.arch))
  {
  }

  void operator()()
  {
    for(auto &f : goto_functions.function_map)
      process_function(f.first, f.second);
  }

protected:
  symbol_tablet &symbol_table;
  goto_functionst &goto_functions;
  message_handlert &message_handler;
  messaget log;
  std::unique_ptr<asm_semanticst> semantics;

  void process_function(const irep_idt &, goto_functionst::goto_functiont &);

  void process_instruction(
    const irep_idt &function_id,
    goto_programt::instructiont &instruction,
    goto_programt &dest);

  void process_instruction_gcc(
    const irep_idt &function_id,
    const code_asm_gcct &,
    goto_programt &dest);

  /// Lower \p code exactly.
  /// \return false, with a reason in \p failure, if it is not understood
  bool lower_gcc(
    const irep_idt &function_id,
    const code_asm_gcct &code,
    goto_programt &dest,
    std::string &failure);

  void process_instruction_msc(
    const irep_idt &,
    const code_asmt &,
    goto_programt &dest);

  /// Approximate a statement that is not understood, with a warning
  void approximate(
    const irep_idt &function_id,
    const code_asmt &code,
    const std::string &reason,
    goto_programt &dest);

  void msc_asm_function_call(
    const irep_idt &function_base_name,
    const exprt::operandst &operands,
    const code_asmt &code,
    goto_programt &dest);

  /// make sure \p function exists in goto_functions
  void declare_function(const irep_idt &function)
  {
    goto_functions.function_map.emplace(function, goto_functiont());
  }
};

/// Adds a call to a library function that implements the given msc-style inline
/// assembly statement
///
/// \param function_base_name: Name of the function to call
/// \param operands: Arguments to be passed to function
/// \param code: msc-style inline assembly statement to translate to function
///   call
/// \param dest: Goto program to append the function call to
void remove_asmt::msc_asm_function_call(
  const irep_idt &function_base_name,
  const exprt::operandst &operands,
  const code_asmt &code,
  goto_programt &dest)
{
  irep_idt function_identifier = function_base_name;

  code_function_callt::argumentst arguments;

  const typet void_pointer = pointer_type(empty_typet());

  for(const auto &op : operands)
    arguments.push_back(typecast_exprt::conditional_cast(op, void_pointer));

  code_typet fkt_type{
    code_typet::parameterst{
      arguments.size(), code_typet::parametert{void_pointer}},
    empty_typet()};

  auto fkt = symbol_exprt{function_identifier, fkt_type}.with_source_location(
    code.source_location());
  code_function_callt function_call(std::move(fkt), std::move(arguments));

  dest.add(
    goto_programt::make_function_call(function_call, code.source_location()));

  // do we have it?
  if(!symbol_table.has_symbol(function_identifier))
  {
    symbolt symbol{function_identifier, fkt_type, ID_C};
    symbol.base_name = function_base_name;

    symbol_table.add(symbol);

    goto_functions.function_map.emplace(function_identifier, goto_functiont());
  }
  else
  {
    DATA_INVARIANT(
      symbol_table.lookup_ref(function_identifier).type == fkt_type,
      "function types should match");
  }
}

/// Translates the given inline assembly code (which must be in either gcc or
/// msc style) to non-assembly goto program instructions
///
/// \param function_id: Name of function being processed
/// \param instruction: The goto program instruction containing the inline
///   assembly statements
/// \param dest: The goto program to append the new instructions to
void remove_asmt::process_instruction(
  const irep_idt &function_id,
  goto_programt::instructiont &instruction,
  goto_programt &dest)
{
  const code_asmt &code = to_code_asm(instruction.get_other());

  const irep_idt &flavor = code.get_flavor();

  if(
    flavor == ID_gcc &&
    (code.operands().size() == 5 || code.operands().size() == 6))
    process_instruction_gcc(function_id, to_code_asm_gcc(code), dest);
  else if(flavor == ID_msc && code.operands().size() == 1)
    process_instruction_msc(function_id, code, dest);
  else
  {
    // e.g., the asm blocks of Apple's and CodeWarrior's compilers
    approximate(function_id, code, "unsupported assembler flavor", dest);
  }
}

void remove_asmt::approximate(
  const irep_idt &function_id,
  const code_asmt &code,
  const std::string &reason,
  goto_programt &dest)
{
  std::string havocked;
  if(
    code.get_flavor() == ID_gcc &&
    (code.operands().size() == 5 || code.operands().size() == 6))
    havocked = approximate_gcc_asm(code, symbol_table, dest);
  else
  {
    // without an operand interface, havoc the C variables named in the text
    std::vector<exprt> lvalues;
    if(!code.operands().empty() && code.op0().id() == ID_string_constant)
    {
      const std::string text =
        id2string(to_string_constant(code.op0()).value());
      std::set<irep_idt> seen;
      std::size_t i = 0;
      while(i < text.size())
      {
        if(std::isalpha(static_cast<unsigned char>(text[i])) || text[i] == '_')
        {
          std::size_t start = i;
          while(i < text.size() &&
                (std::isalnum(static_cast<unsigned char>(text[i])) ||
                 text[i] == '_'))
          {
            ++i;
          }
          const irep_idt name = text.substr(start, i - start);
          if(!seen.insert(name).second)
            continue;
          const symbolt *found = nullptr;
          for(const auto &entry :
              equal_range(symbol_table.symbol_base_map, name))
          {
            const symbolt &candidate = symbol_table.lookup_ref(entry.second);
            if(
              candidate.is_type || candidate.type.id() == ID_code ||
              !candidate.is_lvalue)
            {
              continue;
            }
            if(has_prefix(
                 id2string(entry.second), id2string(function_id) + "::"))
            {
              found = &candidate;
              break;
            }
            if(entry.second == name)
              found = &candidate;
          }
          if(found != nullptr)
            lvalues.push_back(found->symbol_expr());
        }
        else
          ++i;
      }
    }
    havocked = approximate_asm_block(code, lvalues, symbol_table, dest);
  }

  log.warning().source_location = code.source_location();
  log.warning() << "inline assembly not understood (" << reason
                << "); approximating it by havocking " << havocked
                << messaget::eom;
}

/// Translates the given inline assembly code (in gcc style) to non-assembly
/// goto program instructions: exactly, if every instruction is understood,
/// and otherwise by a conservative approximation.
///
/// \param function_id: Name of function being processed
/// \param code: The inline assembly code statement to translate
/// \param dest: The goto program to append the new instructions to
void remove_asmt::process_instruction_gcc(
  const irep_idt &function_id,
  const code_asm_gcct &code,
  goto_programt &dest)
{
  goto_programt tmp;
  std::string failure;
  if(lower_gcc(function_id, code, tmp, failure))
    dest.destructive_append(tmp);
  else
    approximate(function_id, code, failure, dest);
}

bool remove_asmt::lower_gcc(
  const irep_idt &function_id,
  const code_asm_gcct &code,
  goto_programt &dest,
  std::string &failure)
{
  if(code.asm_text().id() != ID_string_constant)
  {
    failure = "template is not a string";
    return false;
  }

  const std::string text =
    id2string(to_string_constant(code.asm_text()).value());

  asm_operandst operands;
  if(!operands.parse(code, semantics.get(), failure))
    return false;

  std::vector<asm_instructiont> instructions;
  if(!split_asm_template(
       text,
       semantics->syntax(),
       operands.is_basic ? nullptr : &operands,
       instructions,
       failure))
  {
    return false;
  }

  asm_lowering_contextt ctx(
    symbol_table, function_id, code.source_location(), operands, *semantics);
  if(code.has_goto_target())
    ctx.goto_target = code.goto_target();

  if(!ctx.prepare())
  {
    failure = ctx.failure;
    return false;
  }

  std::vector<std::string> pending_prefixes;
  std::size_t section_depth = 0;

  for(const auto &original : instructions)
  {
    asm_instructiont instruction = original;
    const std::string &m = instruction.mnemonic;

    // code in other sections is not executed here
    if(m == ".pushsection" || m == ".section")
    {
      ++section_depth;
      continue;
    }
    if(m == ".popsection" || m == ".previous")
    {
      if(section_depth == 0)
      {
        failure = "unbalanced " + m;
        return false;
      }
      --section_depth;
      continue;
    }
    if(section_depth != 0)
      continue;

    for(const auto &label : instruction.labels)
      ctx.define_label(label);

    if(m.empty())
    {
      // a prefix on its own, as in "lock; orq ..."
      pending_prefixes.insert(
        pending_prefixes.end(),
        instruction.prefixes.begin(),
        instruction.prefixes.end());
      continue;
    }

    if(!pending_prefixes.empty())
    {
      instruction.prefixes.insert(
        instruction.prefixes.begin(),
        pending_prefixes.begin(),
        pending_prefixes.end());
      pending_prefixes.clear();
    }

    if(instruction.is_directive())
    {
      static const std::set<std::string> no_effect = {
        ".arch_extension",
        ".arch",
        ".cpu",
        ".align",
        ".balign",
        ".p2align",
        ".syntax",
        ".att_syntax",
        ".thumb",
        ".arm",
        ".code"};
      if(no_effect.count(m) != 0)
        continue;
      failure = "unsupported directive '" + instruction.text + "'";
      return false;
    }

    if(semantics->lower(instruction, ctx))
      continue;

    // barriers are understood on every architecture
    const std::string reason = ctx.failure;
    ctx.failure.clear();
    if(lower_barrier(instruction, ctx))
      continue;
    ctx.failure = reason;

    failure = ctx.failure.empty()
                ? "unsupported instruction '" + instruction.text + "'"
                : ctx.failure + " in '" + instruction.text + "'";
    return false;
  }

  if(!pending_prefixes.empty())
  {
    failure = "prefix without instruction";
    return false;
  }

  if(section_depth != 0)
  {
    failure = "unterminated section";
    return false;
  }

  if(!ctx.finish(dest))
  {
    failure = ctx.failure;
    return false;
  }

  for(const auto &function : ctx.called_functions)
    declare_function(function);

  return true;
}

/// Translates the given inline assembly code (in msc style) to non-assembly
/// goto program instructions
///
/// \param function_id: Name of function being processed
/// \param code: The inline assembly code statement to translate
/// \param dest: The goto program to append the new instructions to
void remove_asmt::process_instruction_msc(
  const irep_idt &function_id,
  const code_asmt &code,
  goto_programt &dest)
{
  const irep_idt &i_str = to_string_constant(code.op0()).value();

  std::istringstream str(id2string(i_str));
  assembler_parsert assembler_parser{message_handler};
  assembler_parser.in = &str;
  assembler_parser.parse();

  goto_programt tmp_dest;
  bool unknown = false;
  std::string unknown_instruction;
  bool x86_32_locked_atomic = false;

  for(const auto &instruction : assembler_parser.instructions)
  {
    if(instruction.empty())
      continue;

#if 0
    std::cout << "A ********************\n";
    for(const auto &ins : instruction)
    {
      std::cout << "XX: " << ins.pretty() << '\n';
    }

    std::cout << "B ********************\n";
#endif

    // deal with prefixes
    irep_idt command;
    unsigned pos = 0;

    if(
      instruction.front().id() == ID_symbol &&
      instruction.front().get(ID_identifier) == "lock")
    {
      x86_32_locked_atomic = true;
      pos++;
    }

    // done?
    if(pos == instruction.size())
      continue;

    if(instruction[pos].id() == ID_symbol)
    {
      command = instruction[pos].get(ID_identifier);
      pos++;
    }

    if(command == "xchg" || command == "xchgl")
      x86_32_locked_atomic = true;

    if(x86_32_locked_atomic)
    {
      tmp_dest.add(goto_programt::make_atomic_begin(code.source_location()));

      codet code_fence(ID_fence);
      code_fence.add_source_location() = code.source_location();
      code_fence.set(ID_WWfence, true);
      code_fence.set(ID_RRfence, true);
      code_fence.set(ID_RWfence, true);
      code_fence.set(ID_WRfence, true);

      tmp_dest.add(
        goto_programt::make_other(code_fence, code.source_location()));
    }

    if(command == "fstcw" || command == "fnstcw" || command == "fldcw") // x86
    {
      exprt::operandst args{null_pointer_exprt{pointer_type(empty_typet{})}};
      // try to typecheck the argument
      if(pos != instruction.size() && instruction[pos].id() == ID_symbol)
      {
        const irep_idt &name = instruction[pos].get(ID_identifier);
        for(const auto &entry : equal_range(symbol_table.symbol_base_map, name))
        {
          // global scope symbol, don't replace a local one
          if(entry.second == name && args[0].id() != ID_address_of)
          {
            args[0] =
              address_of_exprt{symbol_table.lookup_ref(name).symbol_expr()};
          }
          // parameter or symbol in local scope
          else if(has_prefix(
                    id2string(entry.second), id2string(function_id) + "::"))
          {
            args[0] = address_of_exprt{
              symbol_table.lookup_ref(entry.second).symbol_expr()};
          }
        }
      }
      msc_asm_function_call(
        "__asm_" + id2string(command), args, code, tmp_dest);
    }
    else if(
      command == "mfence" || command == "lfence" || command == "sfence") // x86
    {
      msc_asm_function_call("__asm_" + id2string(command), {}, code, tmp_dest);
    }
    else
    {
      unknown = true; // give up
      unknown_instruction = id2string(command);
    }

    if(x86_32_locked_atomic)
    {
      tmp_dest.add(goto_programt::make_atomic_end(code.source_location()));

      x86_32_locked_atomic = false;
    }
  }

  if(unknown)
  {
    approximate(
      function_id,
      code,
      "unsupported instruction '" + unknown_instruction + "'",
      dest);
  }
  else
    dest.destructive_append(tmp_dest);
}

/// Replaces inline assembly instructions in the goto function by non-assembly
/// goto program instructions
///
/// \param function_id: Name of function being processed
/// \param goto_function: The goto function
void remove_asmt::process_function(
  const irep_idt &function_id,
  goto_functionst::goto_functiont &goto_function)
{
  bool did_something = false;

  Forall_goto_program_instructions(it, goto_function.body)
  {
    if(it->is_other() && it->get_other().get_statement() == ID_asm)
    {
      goto_programt tmp_dest;
      process_instruction(function_id, *it, tmp_dest);
      it->turn_into_skip();
      did_something = true;

      goto_programt::targett next = it;
      next++;

      goto_function.body.destructive_insert(next, tmp_dest);
    }
  }

  if(did_something)
  {
    remove_skip(goto_function.body);
    goto_function.body.update();
  }
}

/// \copybrief remove_asm(goto_modelt &, message_handlert &)
///
/// \param goto_functions: The goto functions
/// \param symbol_table: The symbol table
/// \param message_handler: Message handler
void remove_asm(
  goto_functionst &goto_functions,
  symbol_tablet &symbol_table,
  message_handlert &message_handler)
{
  remove_asmt rem(symbol_table, goto_functions, message_handler);
  rem();
}

/// Replaces inline assembly instructions in the goto program (i.e.,
/// instructions of kind `OTHER` with a `code` member of type `code_asmt`) with
/// an appropriate (sequence of) non-assembly goto program instruction(s). At
/// present only a small number of x86 and Power instructions are supported.
/// Unrecognised assembly instructions are ignored.
///
/// \param goto_model: The goto model
/// \param message_handler: Message handler
void remove_asm(goto_modelt &goto_model, message_handlert &message_handler)
{
  remove_asm(
    goto_model.goto_functions, goto_model.symbol_table, message_handler);
}

bool has_asm(const goto_functionst &goto_functions)
{
  for(auto &function_it : goto_functions.function_map)
    for(auto &instruction : function_it.second.body.instructions)
      if(
        instruction.is_other() &&
        instruction.get_other().get_statement() == ID_asm)
      {
        return true;
      }

  return false;
}

bool has_asm(const goto_modelt &goto_model)
{
  return has_asm(goto_model.goto_functions);
}
