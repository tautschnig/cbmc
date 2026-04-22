/// \file
/// Python Parse Tree

#include "python_parse_tree.h"

#include <ostream>

void python_parse_treet::output(std::ostream &out) const
{
  out << ast_json << '\n';
}
