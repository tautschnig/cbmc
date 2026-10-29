// Spike B: native Go parser (Go1.27 grammar).
#ifndef GO_SPIKE_PARSER_H
#define GO_SPIKE_PARSER_H

#include "go_lexer.h"

#include <string>
#include <vector>

struct nodet
{
  std::string kind;
  std::string text;
  std::vector<nodet> kids;
  std::size_t size() const;
};

/// Parse one source file; throws syntax_errort on failure.
nodet go_parse(const std::string &src);

#endif
