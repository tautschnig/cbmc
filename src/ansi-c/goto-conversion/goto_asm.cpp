/*******************************************************************\

Module: Assembler -> Goto

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

/// \file
/// Assembler -> Goto

#include "goto_convert_class.h"

#include <util/arith_tools.h>
#include <util/c_types.h>
#include <util/expr_util.h>
#include <util/symbol.h>

#include "destructor.h"

void goto_convertt::convert_asm(
  const code_asmt &code,
  goto_programt &dest,
  const irep_idt &mode)
{
  if(code.get_flavor() != ID_gcc || code.operands().size() != 5)
  {
    // copy as OTHER
    copy(code, OTHER, dest);
    return;
  }

  code_asm_gcct new_code = to_code_asm_gcc(code);

  // The operands are evaluated before the statement; side effects in them
  // (function calls, statement expressions) need to be made explicit.
  clean_expr_resultt side_effects;
  for(auto *list : {&new_code.outputs(), &new_code.inputs()})
  {
    for(auto &operand : list->operands())
    {
      if(operand.operands().size() != 2)
        continue;
      exprt &expr = to_binary_expr(operand).op1();
      if(needs_cleaning(expr))
        side_effects.add(clean_expr(expr, mode));
    }
  }
  dest.destructive_append(side_effects.side_effects);

  // asm goto: the statement chooses a label, or falls through, which the
  // lowering of the statement records in a variable that is dispatched on
  // here
  const auto &labels = new_code.labels().operands();
  symbol_exprt goto_target{irep_idt{}, typet{}};
  if(!labels.empty())
  {
    goto_target = new_tmp_symbol(
                    unsigned_int_type(),
                    "asm_goto_target",
                    dest,
                    code.source_location(),
                    mode)
                    .symbol_expr();
    new_code.add(ID_asm_goto_target, goto_target);
  }

  copy(new_code, OTHER, dest);

  // For asm goto, the lowering of the statement assigns goto_target the index
  // (1-based) of the label the block jumped to, or 0 to fall through. Dispatch
  // on it with ordinary conditional gotos, converted through the normal path
  // so that scopes and destructors are handled correctly.
  for(std::size_t i = 0; i < labels.size(); ++i)
  {
    code_gotot code_goto{labels[i].get(ID_identifier)};
    code_goto.add_source_location() = code.source_location();

    code_ifthenelset if_goto{
      equal_exprt{goto_target, from_integer(i + 1, goto_target.type())},
      std::move(code_goto)};
    if_goto.add_source_location() = code.source_location();

    convert(if_goto, dest, mode);
  }

  destruct_locals(side_effects.temporaries, dest, ns);
}
