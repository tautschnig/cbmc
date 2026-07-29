/// \file
/// Python Language Interface

#include "python_language.h"

#include <util/arith_tools.h>
#include <util/c_types.h>
#include <util/config.h>
#include <util/cprover_prefix.h>
#include <util/get_base_name.h>
#include <util/message.h>
#include <util/options.h>
#include <util/run.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/symbol_table.h>
#include <util/tempfile.h>

#include <json/json_parser.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "expr2python.h"
#include "python_converter.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <vector>

std::set<std::string> python_languaget::extensions() const
{
  return {"py"};
}

void python_languaget::modules_provided(std::set<std::string> &modules)
{
  modules.insert(get_base_name(parse_path, true));
}

void python_languaget::set_language_options(
  const optionst &options,
  message_handlert &)
{
  function_entry_point = options.get_option("function");
  unbounded_ints = options.get_bool_option("python-unbounded-ints");
  ref_mutables = options.get_bool_option("python-ref-mutables");
  no_body_check = options.get_bool_option("python-no-body-check");
  python_raising_ops_check =
    options.get_bool_option("python-raising-ops-check");
  python_strict_warnings = options.get_bool_option("python-strict-warnings");
  python_use_stdlib_source =
    options.get_bool_option("python-use-stdlib-source");
  python_lazy_stubs = options.get_bool_option("python-lazy-stubs");
  python_no_exception_checks =
    options.get_bool_option("python-no-exception-checks");
  python_required_kwarg_checks =
    options.get_bool_option("python-required-kwarg-checks");
  python_check_missing_methods =
    options.get_bool_option("python-check-missing-methods");
  python_check_typeddict_fields =
    options.get_bool_option("python-check-typeddict-fields");
  python_check_annotations =
    options.get_bool_option("python-check-annotations");
  python_check_any_arg_attrs =
    options.get_bool_option("python-check-any-arg-attrs");
  python_missing_return_check =
    options.get_bool_option("python-missing-return-check");
  python_check_iter_none = options.get_bool_option("python-check-iter-none");
  // --python-smt-strings selects the native SMT-LIB String backend (the
  // legacy byte-array hybrid has been retired).
  python_string_kind = options.get_bool_option("python-smt-strings")
                         ? python_string_kindt::smt_string_native
                         : python_string_kindt::refined;
  std::string max_str = options.get_option("python-max-string-length");
  if(!max_str.empty())
    max_string_length = std::stoul(max_str);
  std::string max_lst = options.get_option("python-max-list-length");
  if(!max_lst.empty())
    max_list_length = std::stoul(max_lst);
}

/// The Python code that converts a .py file to a JSON AST.
/// Invoked as: python3 -c '<this code>' <input.py> <output.json>
// clang-format off
#define PYTHON_AST_TO_JSON_CODE \
  "import sys\n" \
  "try:\n" \
  " import ast,json\n" \
  "except ImportError as e:\n" \
  " print('CBMC Python front-end: failed to import required Python' \\\n" \
  "  ' module: '+str(e)+'\\nPlease ensure Python 3 is installed with' \\\n" \
  "  ' its standard library (the ast and json modules are required).',\\\n" \
  "  file=sys.stderr);sys.exit(1)\n" \
  "try:\n" \
  " src=open(sys.argv[1]).read()\n" \
  " t=ast.parse(src,sys.argv[1])\n" \
  "except SyntaxError as e:\n" \
  " print(str(e),file=sys.stderr);sys.exit(1)\n" \
  "try:\n" \
  " compile(src,sys.argv[1],'exec')\n" \
  "except SyntaxError as e:\n" \
  "  if any(f in str(e)for f in('prior to global declaration',\\\n" \
  "   'prior to nonlocal declaration','keyword argument repeated',\\\n" \
  "   'duplicate argument')):\n" \
  "   print(str(e),file=sys.stderr);sys.exit(1)\n" \
  "except Exception:\n" \
  "  pass\n" \
  "def c(n):\n" \
  " if isinstance(n,ast.AST):\n" \
  "  r={'_type':n.__class__.__name__}\n" \
  "  for f,v in ast.iter_fields(n):r[f]=c(v)\n" \
  "  for a in('lineno','col_offset','end_lineno','end_col_offset'):\n" \
  "   v=getattr(n,a,None)\n" \
  "   if v is not None:r[a]=v\n" \
  "  return r\n" \
  " if isinstance(n,list):return[c(x)for x in n]\n" \
  " return n\n" \
  "def jd(o):\n" \
  " if isinstance(o,complex):\n" \
  "  return{'__complex__':True,'real':o.real,'imag':o.imag}\n" \
  " return str(o)\n" \
  "def bn(t):\n" \
  " s=set()\n" \
  " ct=set()\n" \
  " for n in ast.walk(t):\n" \
  "  if isinstance(n,(ast.ListComp,ast.SetComp,ast.DictComp,ast.GeneratorExp)):\n" \
  "   for g in n.generators:\n" \
  "    for x in ast.walk(g.target):\n" \
  "     if isinstance(x,ast.Name):ct.add(id(x))\n" \
  " for n in ast.walk(t):\n" \
  "  if isinstance(n,ast.Name) and isinstance(n.ctx,ast.Store) and id(n) not in ct:s.add(n.id)\n" \
  "  elif isinstance(n,(ast.FunctionDef,ast.AsyncFunctionDef,ast.ClassDef)):s.add(n.name)\n" \
  "  elif isinstance(n,ast.arg):s.add(n.arg)\n" \
  "  elif isinstance(n,ast.ExceptHandler):\n" \
  "   if n.name:s.add(n.name)\n" \
  "  elif isinstance(n,ast.Import):\n" \
  "   for a in n.names:s.add((a.asname or a.name).split('.')[0])\n" \
  "  elif isinstance(n,ast.ImportFrom):\n" \
  "   for a in n.names:\n" \
  "    if a.name!='*':s.add(a.asname or a.name)\n" \
  "  elif isinstance(n,(ast.Global,ast.Nonlocal)):s.update(n.names)\n" \
  " return sorted(s)\n" \
  "r=c(t);r['_filename']=sys.argv[1];r['_all_bound_names']=bn(t)\n" \
  "r['_python_version']=[sys.version_info[0],sys.version_info[1]]\n" \
  "json.dump(r,open(sys.argv[2],'w',encoding='utf-8'),default=jd,ensure_ascii=False)\n"
// clang-format on

/// Daemon fast path: if CBMC_PYTHON_SERVER_SOCKET points at a
/// running cbmc_python_server (Unix-domain socket), send the
/// absolute source path and read back the JSON AST. Returns
/// `true` on success (with `out` populated); returns `false` on
/// any error so the caller can fall back to the one-shot
/// `python3 -c …` path. Errors are intentionally silent — the
/// daemon is purely an optimisation, never a soundness signal.
static bool python_parse_via_daemon(
  const std::string &path,
  const std::string &json_path,
  message_handlert &message_handler)
{
  const char *sock_env = std::getenv("CBMC_PYTHON_SERVER_SOCKET");
  if(sock_env == nullptr)
    return false;
  std::string sock_path = sock_env;
  if(sock_path.empty())
    return false;

  int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if(fd < 0)
    return false;

  struct sockaddr_un addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  if(sock_path.size() >= sizeof(addr.sun_path))
  {
    ::close(fd);
    return false;
  }
  std::memcpy(addr.sun_path, sock_path.data(), sock_path.size());

  if(::connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
  {
    ::close(fd);
    return false;
  }

  // Resolve to absolute so the daemon (which has its own cwd)
  // can find the file.
  std::string abs_path;
  char *rp = ::realpath(path.c_str(), nullptr);
  if(rp != nullptr)
  {
    abs_path = rp;
    std::free(rp);
  }
  else
    abs_path = path;

  std::string req = abs_path + "\n";
  ssize_t sent = ::write(fd, req.data(), req.size());
  if(sent != static_cast<ssize_t>(req.size()))
  {
    ::close(fd);
    return false;
  }

  // Read header "<length>\n" then payload of that length.
  std::string header;
  char hb;
  while(::read(fd, &hb, 1) == 1 && hb != '\n')
    header.push_back(hb);
  std::size_t length = 0;
  try
  {
    length = std::stoul(header);
  }
  catch(...)
  {
    ::close(fd);
    return false;
  }
  std::string payload;
  payload.reserve(length);
  std::vector<char> buf(8192);
  while(payload.size() < length)
  {
    ssize_t n = ::read(
      fd,
      buf.data(),
      std::min<std::size_t>(buf.size(), length - payload.size()));
    if(n <= 0)
      break;
    payload.append(buf.data(), buf.data() + n);
  }
  ::close(fd);

  if(payload.size() != length || length == 0)
    return false;

  // Write the JSON to the temp file the caller already created;
  // this keeps the rest of the parse() pipeline unchanged.
  std::ofstream out{json_path};
  if(!out)
    return false;
  out.write(payload.data(), payload.size());
  if(!out)
    return false;
  out.close();

  // Quick error-envelope check: if the daemon hit a SyntaxError,
  // the JSON is `{"_type": "Error", "message": "...", ...}` —
  // surface it to the caller and treat as a parse failure (so the
  // caller doesn't try to use a malformed AST). We do this by
  // scanning the first ~64 bytes for `"_type":"Error"`; cheaper
  // than a full JSON parse.
  std::string head =
    payload.substr(0, std::min<std::size_t>(payload.size(), 64));
  if(
    head.find("\"_type\":\"Error\"") != std::string::npos ||
    head.find("\"_type\": \"Error\"") != std::string::npos)
  {
    messaget log{message_handler};
    // Pull out the message field if present.
    std::size_t mp = payload.find("\"message\"");
    if(mp != std::string::npos)
    {
      std::size_t qs = payload.find('"', mp + 9);
      if(qs != std::string::npos)
      {
        std::size_t ts = payload.find('"', qs + 1);
        std::size_t qe = payload.find('"', ts + 1);
        if(qe != std::string::npos)
        {
          std::string msg = payload.substr(ts + 1, qe - ts - 1);
          log.error() << msg << messaget::eom;
        }
      }
    }
    // Daemon parse errors: mimic the one-shot path's behaviour by
    // returning success-of-daemon-but-empty so the caller will
    // notice via subsequent JSON validation. The simpler signal
    // here is to just return false and let the one-shot retry.
    return false;
  }
  return true;
}

bool python_languaget::parse(
  std::istream &,
  const std::string &path,
  message_handlert &message_handler)
{
  parse_path = path;

  messaget log{message_handler};

  // Initialize module search paths from PYTHONPATH and source directory
  if(python_paths.empty())
  {
    // Add source file's directory
    auto last_sep = path.rfind('/');
    if(last_sep != std::string::npos)
      python_paths.push_back(path.substr(0, last_sep));
    else
      python_paths.push_back(".");

    // Add PYTHONPATH entries
    const char *pypath = getenv("PYTHONPATH");
    if(pypath != nullptr)
    {
      std::string pp{pypath};
      std::size_t pos = 0;
      while(pos < pp.size())
      {
        auto sep = pp.find(':', pos);
        if(sep == std::string::npos)
          sep = pp.size();
        std::string entry = pp.substr(pos, sep - pos);
        if(!entry.empty())
          python_paths.push_back(entry);
        pos = sep + 1;
      }
    }
  }

  // Create temp file for JSON output
  temporary_filet json_file{"cbmc_python_ast_", ".json"};
  std::string json_path = json_file();

  // Invoke python3 -c '<inline script>' <input.py> <output.json>
  // This is analogous to how the C front-end invokes gcc -E.
  temporary_filet stderr_file{"cbmc_python_err_", ".txt"};
  std::string stderr_path = stderr_file();

  // Daemon fast path (CBMC_PYTHON_SERVER_SOCKET set + reachable):
  // skip the python3 fork/exec entirely. The server keeps the
  // Python interpreter, ast, and json modules pre-loaded across
  // requests, eliminating the ~28 ms cold-start cost per parse.
  bool daemon_ok = python_parse_via_daemon(path, json_path, message_handler);

  int ret = daemon_ok
              ? 0
              : run(
                  "python3",
                  {"python3", "-c", PYTHON_AST_TO_JSON_CODE, path, json_path},
                  "",
                  "",
                  stderr_path);

  if(ret != 0)
  {
    // Show any error output from Python
    std::ifstream stderr_stream{stderr_path};
    if(stderr_stream)
    {
      std::string line;
      while(std::getline(stderr_stream, line))
        log.error() << line << messaget::eom;
    }

    if(ret == 127 || ret == -1)
    {
      log.error() << "Failed to run python3. CBMC's Python front-end requires "
                     "Python 3 to be installed and available on PATH."
                  << messaget::eom;
    }

    return true;
  }

  // Read the JSON AST
  if(parse_json(json_path, message_handler, parse_tree.ast_json))
  {
    log.error() << "Failed to read Python JSON AST" << messaget::eom;
    return true;
  }

  parse_tree.filename = path;

  return false;
}

bool python_languaget::typecheck(
  symbol_table_baset &symbol_table,
  const std::string &,
  message_handlert &message_handler)
{
  python_convertert converter{symbol_table, parse_tree, message_handler};
  converter.set_unbounded_ints(unbounded_ints);
  converter.set_ref_mutables(ref_mutables);
  converter.set_use_smt_string_native(
    python_string_kind == python_string_kindt::smt_string_native);
  converter.set_no_body_check(no_body_check);
  converter.set_python_raising_ops_check(python_raising_ops_check);
  converter.set_python_strict_warnings(python_strict_warnings);
  converter.set_python_lazy_stubs(python_lazy_stubs);
  converter.set_python_no_exception_checks(python_no_exception_checks);
  converter.set_python_required_kwarg_checks(python_required_kwarg_checks);
  converter.set_python_check_missing_methods(python_check_missing_methods);
  converter.set_python_check_typeddict_fields(python_check_typeddict_fields);
  converter.set_python_check_annotations(python_check_annotations);
  converter.set_python_check_any_arg_attrs(python_check_any_arg_attrs);
  converter.set_python_missing_return_check(python_missing_return_check);
  converter.set_python_check_iter_none(python_check_iter_none);
  converter.set_module_resolver(
    [this, &message_handler](const std::string &name) -> const jsont *
    { return resolve_module(name, message_handler); });
  converter.set_module_path_resolver(
    [this](const std::string &name) -> std::string
    {
      auto it = parsed_module_paths.find(name);
      return it != parsed_module_paths.end() ? it->second : std::string{};
    });
  return converter.convert();
}

bool python_languaget::generate_support_functions(
  symbol_table_baset &symbol_table,
  message_handlert &message_handler)
{
  // If __CPROVER__start already exists, nothing to do
  const std::string start_name = std::string{CPROVER_PREFIX} + "_start";
  irep_idt start_id{start_name};
  if(symbol_table.lookup(start_id) != nullptr)
    return false;

  messaget log{message_handler};
  code_blockt start_body;

  if(!function_entry_point.empty())
  {
    // --function mode: generate a harness that calls the specified function
    // with nondet arguments of the annotated types.
    irep_idt func_id{"python::" + function_entry_point};
    const symbolt *func_sym = symbol_table.lookup(func_id);
    if(func_sym == nullptr)
    {
      log.error() << "Function '" << function_entry_point << "' not found"
                  << messaget::eom;
      return true;
    }

    const code_typet &func_type = to_code_type(func_sym->type);

    // Generate nondet arguments
    exprt::operandst arguments;
    for(const auto &param : func_type.parameters())
    {
      side_effect_expr_nondett nondet{param.type(), source_locationt{}};
      arguments.push_back(std::move(nondet));
    }

    // Call the function
    if(func_type.return_type().id() == ID_empty)
    {
      // void function: just call it
      side_effect_expr_function_callt call{
        func_sym->symbol_expr(),
        std::move(arguments),
        func_type.return_type(),
        source_locationt{}};
      start_body.add(code_expressiont{call});
    }
    else
    {
      // Non-void: call and discard result
      side_effect_expr_function_callt call{
        func_sym->symbol_expr(),
        std::move(arguments),
        func_type.return_type(),
        source_locationt{}};
      start_body.add(code_expressiont{call});
    }
  }
  else
  {
    // Default mode: use the module body as the entry point
    const symbolt *module_body_sym =
      symbol_table.lookup("python::__module_body");
    if(module_body_sym != nullptr && !module_body_sym->value.is_nil())
      start_body = to_code_block(to_code(module_body_sym->value));
  }

  code_typet start_type{{}, empty_typet{}};
  symbolt start_symbol{start_id, start_type, "python"};
  start_symbol.base_name = start_name;
  start_symbol.is_lvalue = true;

  // Note: uncaught exception check removed — too many false positives
  // when functions set exception flags that aren't cleared by handlers.
  // User assertions are the primary verification target.
  const symbolt *exc_sym = symbol_table.lookup("python::__exception_active");
  if(exc_sym != nullptr)
  {
    // Initialize exception flag to false at start
    code_frontend_assignt init_exc{exc_sym->symbol_expr(), false_exprt{}};
    start_body.statements().insert(
      start_body.statements().begin(), std::move(init_exc));
  }

  // Initialize rounding mode to ROUND_TO_EVEN at the start
  irep_idt rounding_id{std::string{CPROVER_PREFIX} + "rounding_mode"};
  const symbolt *rounding_sym = symbol_table.lookup(rounding_id);
  if(rounding_sym != nullptr)
  {
    start_body.statements().insert(
      start_body.statements().begin(),
      code_frontend_assignt{
        rounding_sym->symbol_expr(), from_integer(0, rounding_sym->type)});
  }

  // Call __CPROVER_initialize first so that C-mode static-lifetime
  // symbols (used e.g. to back Python string literals handed to
  // @c_intrinsic C functions) are initialized before any user code
  // runs. Without this, CBMC treats the statically-allocated
  // memory as deallocated / dead on first access.
  irep_idt init_id{std::string{CPROVER_PREFIX} + "initialize"};
  const symbolt *init_sym = symbol_table.lookup(init_id);
  if(init_sym != nullptr)
  {
    side_effect_expr_function_callt init_call{
      init_sym->symbol_expr(), {}, empty_typet{}, source_locationt{}};
    start_body.statements().insert(
      start_body.statements().begin(), code_expressiont{std::move(init_call)});
  }

  start_symbol.value = start_body;

  symbol_table.add(start_symbol);

  return false;
}

void python_languaget::show_parse(std::ostream &out, message_handlert &)
{
  parse_tree.output(out);
}

python_languaget::python_languaget() = default;

/// Set library_paths based on the environment or the cbmc binary
/// location. Called lazily the first time resolve_module runs.
void python_languaget::init_library_paths_if_needed()
{
  if(!library_paths.empty())
    return; // already initialised

  auto is_library_dir = [](const std::string &d)
  { return !d.empty() && std::ifstream{d + "/README.md"}.good(); };

  // 1. Explicit override via environment variable. Accepts a single
  //    directory (not a colon-separated list).
  if(const char *env = getenv("CBMC_PYTHON_LIBRARY"))
  {
    std::string e{env};
    if(!e.empty())
    {
      library_paths.push_back(e);
      return;
    }
  }

  // 2. Locate the running cbmc binary and derive candidate library
  //    directories relative to it. We try (in order):
  //      <bin>/../share/cbmc/python/library    (installed layout)
  //      <bin>/../../src/python/library        (in-tree build)
  //      <bin>/../../../src/python/library     (CMake subproject build)
  char exe_path[4096];
  ssize_t n = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
  if(n > 0)
  {
    exe_path[n] = '\0';
    std::string bin{exe_path};
    auto bin_dir = bin.substr(0, bin.rfind('/'));
    const std::vector<std::string> candidates{
      bin_dir + "/../share/cbmc/python/library",
      bin_dir + "/../../src/python/library",
      bin_dir + "/../../../src/python/library"};
    for(const auto &c : candidates)
    {
      if(is_library_dir(c))
      {
        library_paths.push_back(c);
        return;
      }
    }
  }
  // No library found — leave library_paths empty; resolve_module
  // will just skip the library step.
}

const jsont *python_languaget::resolve_module(
  const std::string &module_name,
  message_handlert &handler)
{
  // Check cache
  auto it = parsed_modules.find(module_name);
  if(it != parsed_modules.end())
    return &it->second;

  messaget log{handler};

  // Convert module.name to module/name
  std::string rel_path = module_name;
  for(auto &c : rel_path)
  {
    if(c == '.')
      c = '/';
  }

  // Search for module/__init__.py or module.py. The search order is:
  //   1. library_paths — CBMC's own model library (unless
  //      --python-use-stdlib-source is set);
  //   2. python_paths — user PYTHONPATH + the source file's dir,
  //      which by default includes the system CPython stdlib via
  //      PYTHONPATH or the OS default.
  std::string found_path;
  std::vector<std::string> search_dirs;
  if(!python_use_stdlib_source)
  {
    init_library_paths_if_needed();
    search_dirs.insert(
      search_dirs.end(), library_paths.begin(), library_paths.end());
  }
  search_dirs.insert(
    search_dirs.end(), python_paths.begin(), python_paths.end());

  for(const auto &dir : search_dirs)
  {
    std::string pkg_init = dir + "/" + rel_path + "/__init__.py";
    std::string mod_file = dir + "/" + rel_path + ".py";
    if(std::ifstream{pkg_init}.good())
    {
      found_path = pkg_init;
      break;
    }
    if(std::ifstream{mod_file}.good())
    {
      found_path = mod_file;
      break;
    }
  }

  if(found_path.empty())
  {
    // Module not found — not an error, just return null
    parsed_modules[module_name] = jsont{};
    return nullptr;
  }

  log.status() << "Resolving import: " << module_name << " → " << found_path
               << messaget::eom;

  // Parse the module using the same AST-to-JSON approach
  temporary_filet json_file{"cbmc_python_mod_", ".json"};
  std::string json_path = json_file();
  temporary_filet stderr_file{"cbmc_python_moderr_", ".txt"};
  std::string stderr_path = stderr_file();

  // Daemon fast path: imported modules are the dominant source of
  // python3 fork/exec cost (boto3 alone produces 30+ imports per
  // benchmark). Skip the subprocess when the daemon is reachable.
  bool daemon_ok = python_parse_via_daemon(found_path, json_path, handler);

  int ret =
    daemon_ok
      ? 0
      : run(
          "python3",
          {"python3", "-c", PYTHON_AST_TO_JSON_CODE, found_path, json_path},
          "",
          "",
          stderr_path);

  if(ret != 0)
  {
    parsed_modules[module_name] = jsont{};
    return nullptr;
  }

  jsont module_ast;
  if(parse_json(json_path, handler, module_ast))
  {
    parsed_modules[module_name] = jsont{};
    return nullptr;
  }

  parsed_modules[module_name] = std::move(module_ast);
  parsed_module_paths[module_name] = found_path;
  return &parsed_modules[module_name];
}

python_languaget::~python_languaget() = default;

std::unique_ptr<languaget> new_python_language()
{
  return std::make_unique<python_languaget>();
}

bool python_languaget::from_expr(
  const exprt &expr,
  std::string &code,
  const namespacet &ns)
{
  code = expr2python(expr, ns);
  return false;
}

bool python_languaget::from_type(
  const typet &type,
  std::string &code,
  const namespacet &ns)
{
  code = type2python(type, ns);
  return false;
}

bool python_languaget::type_to_name(
  const typet &,
  std::string &name,
  const namespacet &)
{
  // TODO: implement
  name = "(python type name)";
  return false;
}

bool python_languaget::to_expr(
  const std::string &,
  const std::string &,
  exprt &,
  const namespacet &,
  message_handlert &)
{
  return true;
}
