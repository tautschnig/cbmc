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

#include <algorithm>
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
  go_compiler = options.get_option("go-compiler");
  deadlock_check = !options.get_bool_option("go-no-deadlock-check");
  race_check = options.get_bool_option("go-race-check");
  keep_unreachable = options.get_bool_option("go-keep-unreachable");
  nondet_parameters.set(options);
}

/// Whether \p version (of gc) is go1.27 or a go1.27 release candidate or
/// minor release, whose capacities of append rt.GrowCapGC and so on model
/// (and helper/runtime/rt_test.go checks).
static bool is_go1_27(const std::string &version)
{
  const std::string prefix = "go1.27";
  if(version.compare(0, prefix.size(), prefix) != 0)
    return false;
  const std::string rest = version.substr(prefix.size());
  return rest.empty() || rest[0] == '.' || rest.compare(0, 2, "rc") == 0;
}

std::optional<go_append_capacityt>
go_languaget::append_capacity(message_handlert &message_handler) const
{
  // As the C front-end follows the compiler it is configured for, follow
  // the Go compiler on PATH (the one that provides the standard library),
  // unless --go-compiler names one: gc go1.27's or TinyGo's capacities, or
  // any capacity for other compilers (whose choices are not modelled).
  // TinyGo uses the go command's standard library, so that it can only be
  // named.
  messaget log{message_handler};
  if(go_compiler == "gc")
    return go_append_capacityt::GC;
  if(go_compiler == "tinygo")
    return go_append_capacityt::TINYGO;
  if(go_compiler == "any")
    return go_append_capacityt::ANY;
  if(!go_compiler.empty())
  {
    log.error() << "--go-compiler: expected gc, tinygo or any, got '"
                << go_compiler << "'" << messaget::eom;
    return {};
  }

  const std::string &name = parse_tree.compiler;
  const std::string &version = parse_tree.compiler_version;
  if(name == "gc" && is_go1_27(version))
  {
    log.status() << "Go compiler: gc " << version
                 << " (capacities of append as gc's)" << messaget::eom;
    return go_append_capacityt::GC;
  }
  log.warning() << "Go compiler: "
                << (name.empty() ? std::string{"unknown"}
                                 : name + " " + version)
                << ": the capacities of append are only modelled for gc "
                << "go1.27 and TinyGo, any capacity is assumed (see "
                << "--go-compiler)" << messaget::eom;
  return go_append_capacityt::ANY;
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

/// The .go files given to CBMC that have not been loaded yet. CBMC parses
/// each file with its own languaget, but the files form one package (as
/// with `go run a.go b.go`, Go1.27 §Packages: "A package in turn is
/// constructed from one or more source files"), which go/types must check
/// together: the files are collected here and loaded by the first
/// typecheck (or show_parse).
static std::vector<std::string> &pending_files()
{
  static std::vector<std::string> files;
  return files;
}

/// Run the helper with \p arguments; report its diagnostics.
/// \return true on error
static bool run_helper(
  const std::vector<std::string> &arguments,
  message_handlert &message_handler)
{
  messaget log{message_handler};
  temporary_filet stderr_file{"cbmc_go_err_", ".txt"};
  const std::string helper = helper_path();
  std::vector<std::string> argv{helper};
  argv.insert(argv.end(), arguments.begin(), arguments.end());
  const int status = run(helper, argv, "", "", stderr_file());
  if(status == 0)
    return false;

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

bool go_languaget::parse(
  std::istream &,
  const std::string &path,
  message_handlert &message_handler)
{
  // The syntax of each file; type checking needs all files of the package.
  if(run_helper({"-syntax", path}, message_handler))
    return true;
  pending_files().push_back(path);
  return false;
}

bool go_languaget::load_pending(message_handlert &message_handler)
{
  std::vector<std::string> paths;
  paths.swap(pending_files());
  if(paths.empty())
    return false; // loaded by another file's language
  // As the go command presents a package's files (sorted by name), which
  // fixes the order of the init functions (Go1.27 §Package_initialization).
  std::sort(paths.begin(), paths.end());
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
  std::vector<std::string> arguments{"-goarch", goarch, "-o", json_file()};
  if(keep_unreachable)
    arguments.push_back("-no-prune");
  arguments.insert(arguments.end(), paths.begin(), paths.end());
  if(run_helper(arguments, message_handler))
    return true;

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
  if(load_pending(message_handler))
    return true;
  if(parse_tree.files.empty())
    return false; // the files were converted with another file's package
  const auto capacity = append_capacity(message_handler);
  if(!capacity.has_value())
    return true;
  go_convert_optionst options;
  options.append_capacity = *capacity;
  options.nondet = nondet_parameters;
  options.entry_function = function_entry_point;
  options.deadlock_check = deadlock_check;
  options.race_check = race_check;
  return go_convertert{symbol_table, parse_tree, options, message_handler}();
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

  // The program's package is the module of its initialisation function
  // (also with --function, where CBMC calls this on a new instance, without
  // a parse tree).
  const symbolt *init = symbol_table.lookup(go_convertert::program_init_id());
  if(init == nullptr)
  {
    log.error() << "Go front-end: no Go program was converted" << messaget::eom;
    return true;
  }
  const std::string package = id2string(init->module);

  // Go1.27 §Program_execution: "Program execution begins by initializing
  // the program and then invoking the function main in package main."
  code_blockt body;
  body.add(call(symbol_table.lookup_ref(INITIALIZE_FUNCTION), {}));
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
  // Its arguments are arbitrary values (go_convertert::convert_entry).
  if(!to_code_type(function->type).parameters().empty())
  {
    function = symbol_table.lookup("go::$entry");
    if(function == nullptr)
      return true; // reported by the conversion
  }
  exprt::operandst arguments;
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

void go_languaget::show_parse(
  std::ostream &out,
  message_handlert &message_handler)
{
  if(load_pending(message_handler))
    return;
  if(!parse_tree.files.empty())
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
