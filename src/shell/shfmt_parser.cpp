/// \file
/// Parsing shell scripts with shfmt.

#include "shfmt_parser.h"

#include <util/json.h>
#include <util/message.h>
#include <util/run.h>
#include <util/tempfile.h>

#include <json/json_parser.h>

#include "shfmt_reader.h"

#include <cstdlib>
#include <fstream>

/// The shfmt executable: $CBMC_SHFMT, or `shfmt` on the PATH.
static std::string shfmt_executable()
{
  const char *from_env = std::getenv("CBMC_SHFMT");
  if(from_env != nullptr && *from_env != '\0')
    return from_env;
  return "shfmt";
}

bool parse_shell_source(
  const std::string &source,
  const std::string &path,
  shell_programt &program,
  message_handlert &message_handler)
{
  messaget log{message_handler};

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
