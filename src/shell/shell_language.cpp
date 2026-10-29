/// \file
/// Shell language interface.

#include "shell_language.h"

#include <util/json.h>
#include <util/message.h>
#include <util/run.h>
#include <util/tempfile.h>

#include <json/json_parser.h>

#include "shfmt_reader.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

std::unique_ptr<languaget> new_shell_language()
{
  return std::make_unique<shell_languaget>();
}

namespace
{
/// The shfmt executable: $CBMC_SHFMT, or `shfmt` on the PATH.
std::string shfmt_executable()
{
  const char *from_env = std::getenv("CBMC_SHFMT");
  if(from_env != nullptr && *from_env != '\0')
    return from_env;
  return "shfmt";
}
} // namespace

bool shell_languaget::parse(
  std::istream &instream,
  const std::string &path,
  message_handlert &message_handler)
{
  messaget log{message_handler};

  // Read the script's exact bytes: the reader recovers the raw text of
  // arithmetic expansions from shfmt's byte offsets.
  std::ostringstream buffer;
  buffer << instream.rdbuf();
  const std::string source = buffer.str();

  program = shell_programt{};
  program.dialect = detect_shell_dialect(source, path);

  // shfmt's --to-json reads only standard input.
  temporary_filet source_file{"cbmc_shell_src_", ".sh"};
  {
    std::ofstream out{source_file(), std::ios::binary};
    out << source;
  }
  temporary_filet json_file{"cbmc_shell_ast_", ".json"};
  temporary_filet stderr_file{"cbmc_shell_err_", ".txt"};

  const std::string shfmt = shfmt_executable();
  const int ret = run(
    shfmt,
    {shfmt, "-ln", shfmt_dialect_name(program.dialect), "--to-json"},
    source_file(),
    json_file(),
    stderr_file());

  if(ret != 0)
  {
    // shfmt reports "line:col: message"; prefix the script's path.
    std::ifstream errors{stderr_file()};
    std::string line;
    while(std::getline(errors, line))
      log.error() << path << ':' << line << messaget::eom;
    if(ret == 127 || ret == -1)
    {
      log.error() << "failed to run '" << shfmt
                  << "'. The shell front-end requires shfmt "
                     "(https://github.com/mvdan/sh); set CBMC_SHFMT to its "
                     "path if it is not on PATH."
                  << messaget::eom;
    }
    return true;
  }

  jsont json;
  if(parse_json(json_file(), message_handler, json))
  {
    log.error() << "failed to read shfmt's JSON output" << messaget::eom;
    return true;
  }

  return shfmt_json_to_shell_program(
    json, source, path, program, message_handler);
}

bool shell_languaget::typecheck(
  symbol_table_baset &,
  const std::string &,
  message_handlert &message_handler)
{
  // Report an error rather than producing an empty program, which would
  // trivially (and wrongly) verify.
  messaget log{message_handler};
  log.error() << "the shell front-end does not yet convert scripts into "
                 "goto programs; only --show-parse-tree is supported"
              << messaget::eom;
  return true;
}

bool shell_languaget::generate_support_functions(
  symbol_table_baset &,
  message_handlert &)
{
  return false;
}

void shell_languaget::show_parse(std::ostream &out, message_handlert &)
{
  program.output(out);
}

bool shell_languaget::to_expr(
  const std::string &,
  const std::string &,
  exprt &,
  const namespacet &,
  message_handlert &message_handler)
{
  messaget log{message_handler};
  log.error() << "the shell front-end does not support parsing expressions"
              << messaget::eom;
  return true;
}
