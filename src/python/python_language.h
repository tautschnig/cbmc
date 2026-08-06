/// \file
/// Python Language Interface

#ifndef CPROVER_PYTHON_PYTHON_LANGUAGE_H
#define CPROVER_PYTHON_PYTHON_LANGUAGE_H

#include <langapi/language.h>

#include "python_parse_tree.h"
#include "python_types.h"

/// Implements the language interface for Python.
class python_languaget : public languaget
{
public:
  void
  set_language_options(const optionst &options, message_handlert &) override;

  bool parse(
    std::istream &instream,
    const std::string &path,
    message_handlert &message_handler) override;

  bool generate_support_functions(
    symbol_table_baset &symbol_table,
    message_handlert &message_handler) override;

  bool typecheck(
    symbol_table_baset &symbol_table,
    const std::string &module,
    message_handlert &message_handler) override;

  void show_parse(std::ostream &out, message_handlert &) override;

  ~python_languaget() override;
  python_languaget();

  bool from_expr(const exprt &expr, std::string &code, const namespacet &ns)
    override;

  bool from_type(const typet &type, std::string &code, const namespacet &ns)
    override;

  bool type_to_name(const typet &type, std::string &name, const namespacet &ns)
    override;

  bool to_expr(
    const std::string &code,
    const std::string &module,
    exprt &expr,
    const namespacet &ns,
    message_handlert &message_handler) override;

  std::unique_ptr<languaget> new_language() override
  {
    return std::make_unique<python_languaget>();
  }

  std::string id() const override
  {
    return "python";
  }

  std::string description() const override
  {
    return "Python 3";
  }

  std::set<std::string> extensions() const override;

  void modules_provided(std::set<std::string> &modules) override;

protected:
  python_parse_treet parse_tree;
  std::string parse_path;

  /// The function name specified by --function, or empty.
  std::string function_entry_point;

  /// Whether to use mathematical integers instead of int64.
  bool unbounded_ints = false;
  bool ref_mutables = false;
  bool no_body_check = false;
  bool python_raising_ops_check = false;
  bool python_strict_warnings = false;
  /// When true, skip the CBMC Python model library and resolve
  /// imports only via the user's PYTHONPATH / system CPython source.
  /// Controlled by the --python-use-stdlib-source command line flag.
  bool python_use_stdlib_source = false;
  /// When true, imported modules (via PYTHONPATH / library path)
  /// only have their function signatures and class fields
  /// registered — function bodies are NOT converted. Calls to
  /// these functions return nondet. Reduces symex memory when
  /// user code imports large stub trees (e.g. boto3). The main
  /// entry-point file's functions are unaffected.
  /// Controlled by the --python-lazy-stubs command line flag.
  bool python_lazy_stubs = false;
  bool python_no_exception_checks = false;
  bool python_required_kwarg_checks = false;
  bool python_check_missing_methods = false;
  bool python_check_typeddict_fields = false;
  bool python_check_annotations = false;
  /// When true, at each call site `f(args)` where `f`'s parameter
  /// is annotated `Any` (or `python_value_type`) and the caller's
  /// argument has a known concrete class type, emit
  /// attribute-error properties for `param.X(...)` references in
  /// `f`'s body where `X` is not a method on the argument's class.
  /// Catches the `bedrock_data_automation_example` Any-erasure
  /// pattern. Off-by-default (opt-in via --python-check-any-arg-attrs).
  bool python_check_any_arg_attrs = false;
  /// When true, emit a missing-return property at the implicit
  /// fall-through of every function with a non-None return-type
  /// annotation. The property fires only if a control-flow path
  /// reaches the implicit return — i.e. the function declared a
  /// return type but a path falls off without returning a value.
  /// Off-by-default (opt-in via --python-missing-return-check).
  bool python_missing_return_check = false;
  /// PLR §6.13 iterating-None TypeError property. When true,
  /// for-loops and comprehensions emit a check that the
  /// iterable is not None. Opt-in via --python-check-iter-none.
  bool python_check_iter_none = false;
  /// Python string back-end selector. Default
  /// 'python_string_kindt::refined' keeps every string as a
  /// refined-string struct {length, data}. When
  /// 'python_string_kindt::smt_string_native' is set
  /// (--python-smt-strings), strings are the native SMT-LIB String sort and
  /// require an SMT String solver (--cvc5/--z3). See
  /// doc/architectural/python-string-phase2-backend-abstraction.md.
  enum class python_string_kindt
  {
    refined,
    smt_string_native,
  };
  python_string_kindt python_string_kind = python_string_kindt::refined;
  std::size_t max_string_length = PYTHON_MAX_STRING_LENGTH;
  std::size_t max_list_length = PYTHON_MAX_LIST_LENGTH;
  bool python_smt_containers = false;
  bool python_ref_instances = false;

  /// Search paths for module resolution (from PYTHONPATH + source dir).
  std::vector<std::string> python_paths;

  /// Search paths for the CBMC-shipped Python model library. These
  /// are consulted *before* python_paths unless
  /// python_use_stdlib_source is true.
  std::vector<std::string> library_paths;

  /// Set library_paths based on the environment or the cbmc binary
  /// location. Invoked lazily from resolve_module.
  void init_library_paths_if_needed();

  /// Already-parsed module ASTs (to avoid re-parsing)
  std::map<std::string, jsont> parsed_modules;
  /// Map from module name to its resolved file path; used by
  /// the converter to attribute source locations to the
  /// imported file rather than the main source.
  std::map<std::string, std::string> parsed_module_paths;

  /// Resolve and parse a Python module, returning its AST JSON.
  /// Returns nullptr if not found.
  const jsont *
  resolve_module(const std::string &module_name, message_handlert &handler);
};

std::unique_ptr<languaget> new_python_language();

#endif // CPROVER_PYTHON_PYTHON_LANGUAGE_H
