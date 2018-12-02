/*******************************************************************\

Module: Data structures representing statements in a program

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

/// \file
/// Data structure representing different types of statements in a program

#include "std_code.h"

#include "std_expr.h"

/// If this `codet` is a \ref code_blockt (i.e.\ it represents a block of
/// statements), return the unmodified input. Otherwise (i.e.\ the `codet`
/// represents a single statement), convert it to a \ref code_blockt with the
/// original statement as its only operand and return the result.
code_blockt &codet::make_block()
{
  if(get_statement()==ID_block)
    return static_cast<code_blockt &>(*this);

  exprt tmp;
  tmp.swap(*this);

  *this = codet(ID_block);
  move_to_operands(tmp);

  return static_cast<code_blockt &>(*this);
}

/// In the case of a `codet` type that represents multiple statements, return
/// the first of them. Otherwise return the `codet` itself.
codet &codet::first_statement()
{
  const irep_idt &statement=get_statement();

  if(has_operands())
  {
    if(statement==ID_block)
      return to_code(op0()).first_statement();
    else if(statement==ID_label)
      return to_code(op0()).first_statement();
  }

  return *this;
}

/// \copydoc first_statement()
const codet &codet::first_statement() const
{
  const irep_idt &statement=get_statement();

  if(has_operands())
  {
    if(statement==ID_block)
      return to_code(op0()).first_statement();
    else if(statement==ID_label)
      return to_code(op0()).first_statement();
  }

  return *this;
}

/// In the case of a `codet` type that represents multiple statements, return
/// the last of them. Otherwise return the `codet` itself.
codet &codet::last_statement()
{
  const irep_idt &statement=get_statement();

  if(has_operands())
  {
    if(statement==ID_block)
      return to_code(operands().back()).last_statement();
    else if(statement==ID_label)
      return to_code(operands().back()).last_statement();
  }

  return *this;
}

/// \copydoc last_statement()
const codet &codet::last_statement() const
{
  const irep_idt &statement=get_statement();

  if(has_operands())
  {
    if(statement==ID_block)
      return to_code(operands().back()).last_statement();
    else if(statement==ID_label)
      return to_code(operands().back()).last_statement();
  }

  return *this;
}

/// Add all the codets from extra_block to the current code_blockt
/// \param extra_block: The input code_blockt
void code_blockt::append(const code_blockt &extra_block)
{
  statements().reserve(statements().size() + extra_block.statements().size());

  for(const auto &statement : extra_block.statements())
  {
    add(statement);
  }
}

codet &code_blockt::find_last_statement()
{
  codet *last=this;

  while(true)
  {
    const irep_idt &statement=last->get_statement();

    if(statement==ID_block &&
       !to_code_block(*last).statements().empty())
    {
      last=&to_code_block(*last).statements().back();
    }
    else if(statement==ID_label)
    {
      last = &(to_code_label(*last).code());
    }
    else
      break;
  }

  return *last;
}

code_blockt create_fatal_assertion(
  const exprt &condition, const source_locationt &loc)
{
  code_blockt result({code_assertt(condition), code_assumet(condition)});

  for(auto &op : result.statements())
    op.add_source_location() = loc;

  result.add_source_location() = loc;

  return result;
}
