/*******************************************************************\

Module: Go front-end: language interface

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// The Go language interface (`languaget`).

#include "go_language.h"

#include <util/config.h>
#include <util/cprover_prefix.h>
#include <util/json.h>
#include <util/options.h>
#include <util/prefix.h>
#include <util/run.h>
#include <util/std_code.h>
#include <util/suffix.h>
#include <util/symbol_table_base.h>
#include <util/tempfile.h>

#include <json/json_parser.h>
#include <linking/static_lifetime_init.h>

#include "expr2go.h"
#include "go_convert.h"

#include <cstdlib>
#include <fstream>

#ifdef __linux__
#  include <limits.h>
#  include <unistd.h>
#endif

void go_languaget::set_language_options(
  const optionst &options,
  message_handlert &)
{
  function_entry_point = options.get_option("function");
}

std::string go_languaget::target_goarch()
{
  // Map CBMC architectures to gc's GOARCH. The pointer width decides
  // between the 32- and 64-bit variant of a family, so that "--32" on an
  // x86_64 host selects 386.
  const std::string arch = id2string(config.ansi_c.arch);
  const std::size_t width = config.ansi_c.pointer_width;
  const bool wide = width == 64;
  if(width != 32 && width != 64)
    return {};
  if(arch == "x86_64" || arch == "i386" || arch == "x32")
    return wide ? "amd64" : "386";
  if(arch == "arm64" || arch == "arm" || arch == "armel" || arch == "armhf")
    return wide ? "arm64" : "arm";
  if(arch == "mips64el" || arch == "mipsel" || arch == "mipsn32el")
    return wide ? "mips64le" : "mipsle";
  if(arch == "mips64" || arch == "mips" || arch == "mipsn32")
    return wide ? "mips64" : "mips";
  if(arch == "ppc64le" && wide)
    return "ppc64le";
  if(arch == "ppc64" && wide)
    return "ppc64";
  if(arch == "riscv64" && wide)
    return "riscv64";
  if(arch == "s390x" && wide)
    return "s390x";
  if(arch == "loongarch64" && wide)
    return "loong64";
  return {};
}

/// Locate cbmc-go-helper: $CBMC_GO_HELPER, then next to the running
/// executable (where the build puts it), then PATH.
static std::string helper_path()
{
  if(const char *env = std::getenv("CBMC_GO_HELPER"))
    return env;
#ifdef __linux__
  char buffer[PATH_MAX];
  const ssize_t n = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if(n > 0)
  {
    std::string self{buffer, static_cast<std::size_t>(n)};
    const std::string candidate =
      self.substr(0, self.rfind('/') + 1) + "cbmc-go-helper";
    if(std::ifstream{candidate}.good())
      return candidate;
  }
#endif
  return "cbmc-go-helper";
}

bool go_languaget::parse(
  std::istream &,
  const std::string &path,
  message_handlert &message_handler)
{
  messaget log{message_handler};

  const std::string goarch = target_goarch();
  if(goarch.empty())
  {
    log.error() << "Go front-end: no Go port for architecture "
                << config.ansi_c.arch << " with " << config.ansi_c.pointer_width
                << "-bit pointers" << messaget::eom;
    return true;
  }

  temporary_filet json_file{"cbmc_go_", ".json"};
  temporary_filet stderr_file{"cbmc_go_err_", ".txt"};
  const std::string helper = helper_path();
  const int status = run(
    helper,
    {helper, "-goarch", goarch, "-o", json_file(), path},
    "",
    "",
    stderr_file());

  if(status != 0)
  {
    // Exit status 3: syntax or type errors, one per line, already in
    // "file:line:col: message" form.
    std::ifstream errors{stderr_file()};
    std::string line;
    while(std::getline(errors, line))
      log.error() << line << messaget::eom;
    if(status != 3)
    {
      // Not a diagnosed Go error: the helper could not be run (util/run
      // reports a failed exec as status 1) or failed internally.
      log.error() << "Go front-end: " << helper << " failed with status "
                  << status
                  << "; set CBMC_GO_HELPER or put cbmc-go-helper next to "
                     "cbmc or on PATH"
                  << messaget::eom;
    }
    return true;
  }

  jsont json;
  if(parse_json(json_file(), message_handler, json))
  {
    log.error() << "Go front-end: failed to read the helper's output"
                << messaget::eom;
    return true;
  }
  return parse_tree.load_json(json, message_handler);
}

bool go_languaget::typecheck(
  symbol_table_baset &symbol_table,
  const std::string &,
  message_handlert &message_handler)
{
  return go_convertert{symbol_table, parse_tree, message_handler}();
}

bool go_languaget::generate_support_functions(
  symbol_table_baset &symbol_table,
  message_handlert &message_handler)
{
  const irep_idt start_id = CPROVER_PREFIX "_start";
  if(symbol_table.has_symbol(start_id))
    return false;
  messaget log{message_handler};

  // Static-lifetime variables (package-level variables, with their zero
  // values) are set up by __CPROVER_initialize.
  if(!symbol_table.has_symbol(INITIALIZE_FUNCTION))
  {
    symbolt init{INITIALIZE_FUNCTION, code_typet{{}, empty_typet{}}, "go"};
    init.base_name = INITIALIZE_FUNCTION;
    init.value = code_blockt{};
    symbol_table.add(init);
  }
  static_lifetime_init(symbol_table, source_locationt{});

  const auto call = [](const symbolt &function, exprt::operandst arguments)
  {
    return code_expressiont{side_effect_expr_function_callt{
      function.symbol_expr(),
      std::move(arguments),
      to_code_type(function.type).return_type(),
      function.location}};
  };

  // With --function, CBMC calls this on a new instance, without a parse
  // tree; the package is then that of the package initialisation function
  // the conversion created.
  std::string package = parse_tree.package;
  if(package.empty())
  {
    for(const auto &entry : symbol_table.symbols)
    {
      const std::string name = id2string(entry.first);
      const std::string suffix = ".$init";
      if(
        entry.second.mode == "go" && has_prefix(name, "go::") &&
        has_suffix(name, suffix))
      {
        package = name.substr(4, name.size() - 4 - suffix.size());
        break;
      }
    }
  }

  // Go1.27 §Program_execution: "Program execution begins by initializing
  // the program and then invoking the function main in package main."
  code_blockt body;
  body.add(call(symbol_table.lookup_ref(INITIALIZE_FUNCTION), {}));
  if(
    const symbolt *init =
      symbol_table.lookup(go_convertert::package_init_id(package)))
    body.add(call(*init, {}));

  // With --function, call that function with arbitrary arguments instead
  // of main.main.
  const std::string entry =
    function_entry_point.empty() ? "main" : function_entry_point;
  const symbolt *function =
    symbol_table.lookup(go_convertert::symbol_id(package + "." + entry));
  if(function == nullptr)
  {
    log.error() << "Go front-end: function " << package << "." << entry
                << " not found" << messaget::eom;
    return true;
  }
  exprt::operandst arguments;
  for(const auto &parameter : to_code_type(function->type).parameters())
  {
    if(contains_reference(parameter.type(), namespacet{symbol_table}))
    {
      // A pointer or function value is nil or refers to a variable or
      // function; an arbitrary bit pattern would be neither.
      log.error() << "Go front-end: --function with parameters containing "
                     "pointers or functions is not supported yet"
                  << messaget::eom;
      return true;
    }
    arguments.push_back(
      side_effect_expr_nondett{parameter.type(), function->location});
  }
  // Go1.27 §Handling_panics: a panic during initialisation ends the
  // program; one that is not recovered is reported by go::$check_panics.
  const symbolt *panicking = symbol_table.lookup("go::$panicking");
  if(panicking != nullptr)
  {
    code_ifthenelset run{
      not_exprt{panicking->symbol_expr()},
      call(*function, std::move(arguments))};
    body.add(std::move(run));
  }
  else
    body.add(call(*function, std::move(arguments)));
  if(const symbolt *check = symbol_table.lookup("go::$check_panics"))
    body.add(call(*check, {}));

  symbolt start{start_id, code_typet{{}, empty_typet{}}, "go"};
  start.base_name = start_id;
  start.value = std::move(body);
  symbol_table.add(start);
  return false;
}

void go_languaget::show_parse(std::ostream &out, message_handlert &)
{
  parse_tree.output(out);
}

bool go_languaget::from_expr(
  const exprt &expr,
  std::string &code,
  const namespacet &ns)
{
  code = expr2go(expr, ns);
  return false;
}

bool go_languaget::from_type(
  const typet &type,
  std::string &code,
  const namespacet &ns)
{
  code = type2go(type, ns);
  return false;
}

bool go_languaget::type_to_name(
  const typet &type,
  std::string &name,
  const namespacet &ns)
{
  return from_type(type, name, ns);
}

bool go_languaget::to_expr(
  const std::string &,
  const std::string &,
  exprt &,
  const namespacet &,
  message_handlert &message_handler)
{
  messaget{message_handler}.error()
    << "Go front-end: parsing expressions is not supported" << messaget::eom;
  return true;
}

std::unique_ptr<languaget> new_go_language()
{
  return std::make_unique<go_languaget>();
}
