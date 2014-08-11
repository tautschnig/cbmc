/*******************************************************************\

Module: Goto Interpreter Utility 

Author: Siqing Tang, jtang707@gmail.com

\*******************************************************************/

#include <string>

#include "interpreter_util.h"

/*******************************************************************\

Function: is_string_constant

Inputs: 

Outputs: 

Purpose: 

\*******************************************************************/

bool is_string_constant( const exprt &expr)
{
  return expr.op0().id() == ID_index && 
        expr.op0().operands().size() == 2 &&
        expr.op0().op0().id() == ID_string_constant;
}

/*******************************************************************\

Function: is_c_pointer_of_char

Inputs:

Outputs:

Purpose: execute printf() - work for integer/float/double/string/char

\*******************************************************************/

bool is_c_pointer_of_char(typet type)
{
  if (type.id() == ID_pointer)
  {
    const irep_idt id = type.subtype().get(ID_C_c_type);
    if (id == ID_char || id == ID_signed_char || id == ID_unsigned_char)
    {
      return true;
    }
  }

  return false;
}

/*******************************************************************\

Function: find_next_exp_sep

Inputs: a string and staring position in the string (zero-based)

Outputs: position of ./[, if any, or std::string::npos if none

Purpose: find next '.' or '[', whichever comes firsts.

\*******************************************************************/

size_t find_next_exp_sep(std::string str, size_t start)
{
  size_t left_sq_bracket_p  = str.find_first_of("[", start);
  size_t dot_p  = str.find_first_of(".", start);
  if (left_sq_bracket_p != std::string::npos && dot_p != std::string::npos)
  {
    // e.g. pts[0].x where pts is defined as "struct Point pts[3]" and Point as "struct Point {int x; int y;}"
    size_t min_p = left_sq_bracket_p < dot_p ? left_sq_bracket_p : dot_p;
    return min_p;
  }
  else if (left_sq_bracket_p != std::string::npos)
  {
    // e.g. a[0] where a is defined as "int a[3]"
    return left_sq_bracket_p;
  }
  else if (dot_p != std::string::npos)
  {
    // e.g. pt.x where pt is defined as "struct Point {int x; int y;}"
    return dot_p;
  }

  return std::string::npos;
}
