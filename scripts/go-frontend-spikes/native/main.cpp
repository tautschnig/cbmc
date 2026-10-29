// Spike B driver: parse each file named on stdin (one per line) and
// report OK/FAIL with the first error, plus throughput. With --counts,
// print per-file node-kind counts for comparison with the go/parser
// oracle (scripts/go-frontend-spikes/oracle).
#include "go_parser.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

static void count(const nodet &n, std::map<std::string, int> &c)
{
  static const std::set<std::string> tracked = {
    "FuncDecl", "FuncLit",  "CompositeLit", "Call",       "TypeSpec",
    "If",       "Switch",   "For",          "KeyValue",   "Return",
    "Labeled",  "Send",     "IncDec",       "Slice",      "TypeAssert",
    "Select"};
  if(tracked.count(n.kind))
    c[n.kind]++;
  for(const auto &k : n.kids)
    count(k, c);
}

int main(int argc, char **argv)
{
  const bool counts = argc > 1 && std::strcmp(argv[1], "--counts") == 0;
  std::string path;
  std::size_t ok = 0, fail = 0, nodes = 0, bytes = 0;
  auto start = std::chrono::steady_clock::now();
  while(std::getline(std::cin, path))
  {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string src = ss.str();
    bytes += src.size();
    try
    {
      nodet tree = go_parse(src);
      nodes += tree.size();
      ok++;
      if(counts)
      {
        std::map<std::string, int> c;
        count(tree, c);
        std::cout << path;
        for(const auto &kv : c)
          std::cout << ' ' << kv.first << '=' << kv.second;
        std::cout << '\n';
      }
      else
        std::cout << "OK   " << path << '\n';
    }
    catch(const syntax_errort &e)
    {
      fail++;
      std::cout << "FAIL " << path << ':' << e.line << ':' << e.col << ": "
                << e.what() << '\n';
    }
  }
  double secs = std::chrono::duration<double>(
                  std::chrono::steady_clock::now() - start)
                  .count();
  std::cerr << "files ok=" << ok << " fail=" << fail << " nodes=" << nodes
            << " MB=" << bytes / 1e6 << " secs=" << secs << '\n';
  return fail != 0;
}
