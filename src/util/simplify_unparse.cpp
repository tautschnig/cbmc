/*******************************************************************\

Module:

Author: Michael Tautschnig, michael.tautschnig@gmail.com

\*******************************************************************/

#include "simplify_unparse.h"

#include <iostream>

#include "expr.h"

#include "y.tab.h"

/*******************************************************************\

Function: simplify_unparset::simplify_unparset

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

simplify_unparset::simplify_unparset(const exprt &expr)
{
  stack.push(std::make_pair(&expr, OPERAND));
}

/*******************************************************************\

Function: simplify_unparset::yylex

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

int simplify_unparset::yylex()
{
  yysimplifylval=0;

  if(stack.empty())
    return 0;
  else if(stack.top().second==BEGIN_OPERANDS)
  {
    stack.pop();
    return '(';
  }
  else if(stack.top().second==END_OPERANDS)
  {
    stack.pop();
    return ')';
  }

  const exprt &expr=*stack.top().first;
  stack.pop();
  if(expr.has_operands())
  {
    const exprt::operandst &operands=expr.operands();
    assert(!operands.empty());
    stack.push(std::make_pair<const exprt*, markert>(0, END_OPERANDS));
    for(exprt::operandst::const_reverse_iterator
        it=operands.rbegin();
        it!=operands.rend();
        ++it)
      stack.push(std::make_pair(&(*it), OPERAND));
    stack.push(std::make_pair<const exprt*, markert>(0, BEGIN_OPERANDS));
  }

  yysimplifylval=&expr;
  return expr_to_tok(expr);
}

/*******************************************************************\

Function: simplify_unparset::abort_expr

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void simplify_unparset::abort_expr()
{
  bool rm_end_op=!stack.empty() && stack.top().second==BEGIN_OPERANDS;

  // skip the current set of operands
  while(!stack.empty() && stack.top().second!=END_OPERANDS)
    stack.pop();

  if(rm_end_op && !stack.empty() && stack.top().second==END_OPERANDS)
    stack.pop();
}

/*******************************************************************\

Function: simplify_unparset::expr_to_tok

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

int simplify_unparset::expr_to_tok(const exprt& expr) const
{
  const irep_idt& id=expr.id();

  if(id==ID_constant)
    return TOK_constant;
  else if(id==ID_not)
    return TOK_not;
  else if(id==ID_symbol)
    return TOK_symbol;
  else if(id==ID_typecast)
    return TOK_typecast;
  else if(id==ID_infinity)
    return TOK_infinity;
  else
    return TOK_expr;
}

