/*******************************************************************\

Module: Inline assembly: conservative approximation

Author: Kiro

\*******************************************************************/

/// \file
/// The conservative approximation of inline assembly statements

#include "asm_approximation.h"

#include <util/arith_tools.h>
#include <util/cprover_prefix.h>
#include <util/pointer_expr.h>
#include <util/prefix.h>
#include <util/std_code.h>
#include <util/symbol_table_base.h>

#include <goto-programs/goto_program.h>

static bool is_constant_type(const typet &type)
{
  if(type.get_bool(ID_C_constant))
    return true;
  if(type.id() == ID_array)
    return is_constant_type(to_array_type(type).element_type());
  return false;
}

/// havoc \p lvalue
static void havoc_lvalue(
  const exprt &lvalue,
  const source_locationt &location,
  goto_programt &dest)
{
  dest.add(goto_programt::make_assignment(
    lvalue, side_effect_expr_nondett{lvalue.type(), location}, location));
}

/// havoc the object \p pointer points to, if it is not null
static void havoc_pointee(
  const exprt &pointer,
  const source_locationt &location,
  goto_programt &dest)
{
  const typet &base = to_pointer_type(pointer.type()).base_type();
  if(base.id() == ID_code)
    return;

  // havoc_object does not apply to a null pointer
  auto skip = dest.add(goto_programt::make_incomplete_goto(
    equal_exprt{pointer, null_pointer_exprt{to_pointer_type(pointer.type())}},
    location));

  codet havoc{ID_havoc_object};
  havoc.add_source_location() = location;
  havoc.add_to_operands(pointer);
  dest.add(goto_programt::make_other(havoc, location));

  skip->complete_goto(dest.add(goto_programt::make_skip(location)));
}

/// havoc all objects of static lifetime that the program may write
static std::size_t havoc_static_objects(
  const symbol_table_baset &symbol_table,
  const source_locationt &location,
  goto_programt &dest)
{
  std::size_t count = 0;
  for(const auto &entry : symbol_table.symbols)
  {
    const symbolt &symbol = entry.second;
    if(
      !symbol.is_static_lifetime || symbol.is_type || symbol.is_macro ||
      !symbol.is_lvalue || symbol.type.id() == ID_code ||
      symbol.type.id() == ID_empty || is_constant_type(symbol.type) ||
      has_prefix(id2string(symbol.name), CPROVER_PREFIX) ||
      has_prefix(id2string(symbol.base_name), CPROVER_PREFIX))
    {
      continue;
    }

    havoc_lvalue(symbol.symbol_expr(), location, dest);
    ++count;
  }
  return count;
}

std::string approximate_gcc_asm(
  const code_asmt &code,
  const symbol_table_baset &symbol_table,
  goto_programt &dest)
{
  const source_locationt &location = code.source_location();
  std::string description;

  if(code.operands().size() != 5)
    return "nothing";

  const code_asm_gcct &gcc = static_cast<const code_asm_gcct &>(code);

  std::size_t outputs = 0;
  for(const auto &output : gcc.outputs().operands())
  {
    if(output.operands().size() != 2)
      continue;
    havoc_lvalue(to_binary_expr(output).op1(), location, dest);
    ++outputs;
  }
  description =
    std::to_string(outputs) + " output operand" + (outputs == 1 ? "" : "s");

  bool memory = false;
  for(const auto &clobber : gcc.clobbers().operands())
  {
    const exprt &name = clobber.id() == ID_gcc_asm_clobbered_register
                          ? to_unary_expr(clobber).op()
                          : clobber;
    if(name.id() == ID_string_constant && name.get(ID_value) == "memory")
      memory = true;
  }

  if(memory)
  {
    std::size_t pointees = 0;
    for(const auto *list : {&gcc.outputs(), &gcc.inputs()})
    {
      for(const auto &operand : list->operands())
      {
        if(operand.operands().size() != 2)
          continue;
        const exprt &expr = to_binary_expr(operand).op1();
        if(expr.type().id() == ID_pointer)
        {
          havoc_pointee(expr, location, dest);
          ++pointees;
        }
      }
    }
    const std::size_t statics =
      havoc_static_objects(symbol_table, location, dest);
    description += ", the objects pointed to by " + std::to_string(pointees) +
                   " pointer operand" + (pointees == 1 ? "" : "s") + ", and " +
                   std::to_string(statics) +
                   " objects of static lifetime (\"memory\" clobber; other "
                   "objects are not havocked)";
  }

  const exprt &goto_target =
    static_cast<const exprt &>(code.find(ID_asm_goto_target));
  if(goto_target.is_not_nil())
  {
    havoc_lvalue(goto_target, location, dest);
    description += ", and the choice of asm goto label";
  }

  return description;
}

std::string approximate_asm_block(
  const code_asmt &code,
  const std::vector<exprt> &lvalues,
  const symbol_table_baset &symbol_table,
  goto_programt &dest)
{
  const source_locationt &location = code.source_location();

  for(const auto &lvalue : lvalues)
  {
    havoc_lvalue(lvalue, location, dest);
    if(lvalue.type().id() == ID_pointer)
      havoc_pointee(lvalue, location, dest);
  }

  const std::size_t statics =
    havoc_static_objects(symbol_table, location, dest);

  return std::to_string(lvalues.size()) + " named variable" +
         (lvalues.size() == 1 ? "" : "s") +
         " (and the objects they point to) and " + std::to_string(statics) +
         " objects of static lifetime (other objects are not havocked)";
}
