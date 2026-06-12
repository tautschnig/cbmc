/*******************************************************************\

Module: COBOL Language Interface

Author: Kiro

\*******************************************************************/

/// \file
/// COBOL Language Interface

#include "cobol_language.h"

#include <util/get_base_name.h>
#include <util/symbol_table.h>

#include <linking/linking.h>
#include <linking/remove_internal_symbols.h>

#include "cobol_entry_point.h"
#include "cobol_typecheck.h"
#include "expr2cobol.h"

#include <cstdlib>

/// Directories searched for copybooks, in order. Covers the source file's own
/// directory, the conventional CardDemo-style sibling/child cpy and cpy-bms
/// directories, the GnuCOBOL COBCPY environment variable (GnuCOBOL
/// Programmer's Guide, "COPY" / library search), and the current directory.
static std::vector<std::string> copybook_dirs(const std::string &path)
{
  std::vector<std::string> dirs;
  const std::size_t slash = path.find_last_of("/\\");
  const std::string dir =
    slash == std::string::npos ? "." : path.substr(0, slash);

  dirs.push_back(dir);
  dirs.push_back(dir + "/../cpy");
  dirs.push_back(dir + "/../cpy-bms");
  dirs.push_back(dir + "/cpy");
  dirs.push_back(dir + "/cpy-bms");
  // Shared copybook libraries one level up (e.g. a sub-application's programs
  // copying from the common app/cpy directory).
  dirs.push_back(dir + "/../../cpy");
  dirs.push_back(dir + "/../../cpy-bms");
  // DB2 DCLGEN members (host-variable structures included via EXEC SQL
  // INCLUDE) are conventionally kept in a dcl/ directory.
  dirs.push_back(dir + "/../dcl");
  dirs.push_back(dir + "/dcl");
  dirs.push_back(dir + "/../../dcl");

  if(const char *cobcpy = std::getenv("COBCPY"))
  {
    std::string s{cobcpy};
    std::size_t start = 0;
    while(start <= s.size())
    {
      const std::size_t sep = s.find(':', start);
      const std::size_t end = sep == std::string::npos ? s.size() : sep;
      if(end > start)
        dirs.push_back(s.substr(start, end - start));
      if(sep == std::string::npos)
        break;
      start = sep + 1;
    }
  }

  dirs.emplace_back(".");
  return dirs;
}

bool cobol_languaget::parse(
  std::istream &instream,
  const std::string &path,
  message_handlert &message_handler)
{
  parse_path = path;
  tokens = cobol_scan(instream, path);
  tokens =
    cobol_expand_copy(std::move(tokens), copybook_dirs(path), message_handler);
  return false;
}

bool cobol_languaget::typecheck(
  symbol_table_baset &symbol_table,
  const std::string &module,
  message_handlert &message_handler)
{
  symbol_tablet new_symbol_table;

  if(cobol_typecheck(tokens, new_symbol_table, module, message_handler))
    return true;

  remove_internal_symbols(new_symbol_table, message_handler, true);

  return linking(symbol_table, new_symbol_table, message_handler);
}

bool cobol_languaget::generate_support_functions(
  symbol_table_baset &symbol_table,
  message_handlert &message_handler)
{
  return cobol_entry_point(symbol_table, message_handler);
}

void cobol_languaget::show_parse(std::ostream &out, message_handlert &)
{
  for(const auto &token : tokens)
  {
    if(token.kind == cobol_token_kindt::END_OF_FILE)
      break;
    out << token.text << '\n';
  }
}

bool cobol_languaget::from_expr(
  const exprt &expr,
  std::string &code,
  const namespacet &ns)
{
  code = expr2cobol(expr, ns);
  return false;
}

bool cobol_languaget::from_type(
  const typet &type,
  std::string &code,
  const namespacet &ns)
{
  code = type2cobol(type, ns);
  return false;
}

bool cobol_languaget::to_expr(
  const std::string &,
  const std::string &,
  exprt &,
  const namespacet &,
  message_handlert &)
{
  // Not supported.
  return true;
}

std::set<std::string> cobol_languaget::extensions() const
{
  return {"cob", "cbl", "cobol", "CBL", "COB", "COBOL", "Cbl", "Cob"};
}

void cobol_languaget::modules_provided(std::set<std::string> &modules)
{
  modules.insert(get_base_name(parse_path, true));
}

std::unique_ptr<languaget> cobol_languaget::new_language()
{
  return std::make_unique<cobol_languaget>();
}

std::unique_ptr<languaget> new_cobol_language()
{
  return std::make_unique<cobol_languaget>();
}
