/// Internal helper functions shared between python_converter.cpp
/// and its source-split siblings (python_converter_compare.cpp,
/// etc.). These are inline so each translation unit gets its own
/// copy and avoids multiple-definition link errors.
///
/// Kept out of python_converter.h because they are implementation
/// details, not part of the converter's public interface.

#ifndef CPROVER_PYTHON_CONVERTER_HELPERS_H
#define CPROVER_PYTHON_CONVERTER_HELPERS_H

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/mathematical_expr.h>
#include <util/mathematical_types.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/symbol.h>

#include <string>
#include <vector>

/// Emit a two-string-argument bool-returning intrinsic call
/// (e.g., cprover_string_equal_func, cprover_string_contains_func).
/// Registers the function in the symbol table, creates a fresh
/// result symbol, assigns the function application into it via
/// pending_checks, and returns the result as a bool_typet.
[[maybe_unused]] static inline exprt emit_string_bool_function(
  const irep_idt &func_id,
  const exprt &str1,
  const exprt &str2,
  symbol_table_baset &symbol_table,
  std::vector<codet> &pending_checks)
{
  const typet c_bool = c_bool_typet(8);
  irep_idt sym_id{func_id};
  if(symbol_table.lookup(sym_id) == nullptr)
  {
    std::vector<typet> arg_types;
    arg_types.push_back(str1.type());
    arg_types.push_back(str2.type());
    symbolt fs{
      sym_id,
      mathematical_function_typet(std::move(arg_types), c_bool),
      "python"};
    fs.base_name = id2string(func_id);
    symbol_table.add(fs);
  }

  function_application_exprt app(
    symbol_table.lookup_ref(sym_id).symbol_expr(), {str1, str2});
  app.type() = c_bool;

  // Use pending_checks.size() as a monotonic-ish uniquifier —
  // different translation units would otherwise each have their
  // own counter and risk name collisions in the shared symbol
  // table. pending_checks grows as the converter emits, so the
  // size alone uniquely identifies each call site at emission
  // time.
  std::string rc_name = "__str_eq_" + std::to_string(pending_checks.size()) +
                        "_" + std::to_string(symbol_table.symbols.size());
  irep_idt rc_id{"python::" + rc_name};
  if(symbol_table.lookup(rc_id) == nullptr)
  {
    symbolt rs{rc_id, c_bool, "python"};
    rs.base_name = rc_name;
    rs.is_lvalue = true;
    rs.is_state_var = true;
    symbol_table.add(rs);
  }
  pending_checks.push_back(
    code_frontend_assignt{symbol_table.lookup_ref(rc_id).symbol_expr(), app});

  return typecast_exprt(
    symbol_table.lookup_ref(rc_id).symbol_expr(), bool_typet());
}

#endif
