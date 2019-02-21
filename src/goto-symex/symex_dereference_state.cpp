/*******************************************************************\

Module: Symbolic Execution of ANSI-C

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

/// \file
/// Symbolic Execution of ANSI-C

#include "symex_dereference_state.h"

#include <util/symbol_table.h>

/// Get or create a failed symbol for the given pointer-typed expression. These
/// are used as placeholders when dereferencing expressions that are illegal to
/// dereference, such as null pointers. The \ref add_failed_symbols pass must
/// have been run for this to do anything useful;  it annotates a pointer-typed
/// symbol `p` with an `ID_C_failed_symbol` comment, which we then clone on
/// demand due to L1 renaming.
///
/// For example, if \p expr is `p` and it has an `ID_C_failed_symbol` `p$object`
/// (the naming convention used by `add_failed_symbols`), and the latest L1
/// renaming of `p` is `p!2@4`, then we will create `p$object!2@4` if it doesn't
/// already exist.
///
/// \param expr: expression to get or create a failed symbol for
/// \return pointer to the failed symbol for \p expr, or nullptr if none
const symbolt *
symex_dereference_statet::get_or_create_failed_symbol(const exprt &expr)
{
  const namespacet &ns=goto_symex.ns;

  if(expr.id()==ID_symbol &&
     expr.get_bool(ID_C_SSA_symbol))
  {
    const ssa_exprt &ssa_expr=to_ssa_expr(expr);
    if(ssa_expr.get_original_expr().id()!=ID_symbol)
      return nullptr;

    const symbolt &ptr_symbol=
      ns.lookup(to_ssa_expr(expr).get_object_name());

    const irep_idt &failed_symbol = ptr_symbol.type.get(ID_C_failed_symbol);

    const symbolt *symbol;
    if(failed_symbol!="" &&
        !ns.lookup(failed_symbol, symbol))
    {
      symbolt sym=*symbol;
      symbolt *sym_ptr=nullptr;
      symbol_exprt sym_expr=sym.symbol_expr();
      state.rename(sym_expr, ns, field_sensitivity, goto_symex_statet::L1);
      sym.name=to_ssa_expr(sym_expr).get_identifier();
      state.symbol_table.move(sym, sym_ptr);
      return sym_ptr;
    }
  }
  else if(expr.id()==ID_symbol)
  {
    const symbolt &ptr_symbol=
      ns.lookup(to_symbol_expr(expr).get_identifier());

    const irep_idt &failed_symbol = ptr_symbol.type.get(ID_C_failed_symbol);

    const symbolt *symbol;
    if(failed_symbol!="" &&
        !ns.lookup(failed_symbol, symbol))
    {
      symbolt sym=*symbol;
      symbolt *sym_ptr=nullptr;
      symbol_exprt sym_expr=sym.symbol_expr();
      state.rename(sym_expr, ns, field_sensitivity, goto_symex_statet::L1);
      sym.name=to_ssa_expr(sym_expr).get_identifier();
      state.symbol_table.move(sym, sym_ptr);
      return sym_ptr;
    }
  }

  return nullptr;
}

/// Just forwards a value-set query to `state.value_set`
void symex_dereference_statet::get_value_set(
  const exprt &expr,
  value_setst::valuest &value_set) const
{
  state.value_set.get_value_set(expr, value_set, goto_symex.ns);

  #if 0
  std::cout << "**************************\n";
  state.value_set.output(goto_symex.ns, std::cout);
  std::cout << "**************************\n";
  #endif

  #if 0
  std::cout << "E: " << from_expr(goto_symex.ns, "", expr) << '\n';
  #endif

  #if 0
  std::cout << "**************************\n";
  for(value_setst::valuest::const_iterator it=value_set.begin();
      it!=value_set.end();
      it++)
    std::cout << from_expr(goto_symex.ns, "", *it) << '\n';
  std::cout << "**************************\n";
  #endif
}
