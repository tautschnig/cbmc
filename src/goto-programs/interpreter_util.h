/*******************************************************************\

   Class: interpretert_util

 Purpose: interpreter utility functions

\*******************************************************************/

#ifndef GOTO_INTERPRETER_UTIL_H
#define GOTO_INTERPRETER_UTIL_H

#include <string>
#include <util/expr.h>
#include <util/type.h>

bool is_string_constant( const exprt &expr);
bool is_c_pointer_of_char(typet type);
size_t find_next_exp_sep(std::string str, size_t start);

#endif /* GOTO_INTERPRETER_UTIL_H */
