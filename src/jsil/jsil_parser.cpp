/*******************************************************************\

Module: Jsil Language

Author: Michael Tautschnig, tautschn@amazon.com

\*******************************************************************/

/// \file
/// Jsil Language

#include "jsil_parser.h"

int yyjsillex_init_extra(jsil_parsert *, void **);
int yyjsillex_destroy(void *);
int yyjsilparse(jsil_parsert &, void *);

bool jsil_parsert::parse()
{
  void *scanner;
  yyjsillex_init_extra(this, &scanner);
  bool parse_fail = yyjsilparse(*this, scanner) != 0;
  yyjsillex_destroy(scanner);
  return parse_fail;
}
