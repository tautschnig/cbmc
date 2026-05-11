/// \file
/// Python to GOTO converter — converts Python JSON AST to CBMC symbol table

#ifndef CPROVER_PYTHON_PYTHON_CONVERTER_H
#define CPROVER_PYTHON_PYTHON_CONVERTER_H

#include <util/bitvector_types.h>
#include <util/mathematical_types.h>
#include <util/message.h>
#include <util/namespace.h>
#include <util/std_code.h>
#include <util/symbol_table_base.h>

#include "python_parse_tree.h"

#include <functional>
#include <map>
#include <optional>
#include <set>

/// Converts a Python JSON AST into CBMC's symbol table representation.
/// This populates the symbol table with function symbols and their bodies
/// as codet trees, which are then converted to goto programs by the
/// standard goto_convert pipeline.
class python_convertert
{
public:
  python_convertert(
    symbol_table_baset &_symbol_table,
    const python_parse_treet &_parse_tree,
    message_handlert &_message_handler);

  /// Main entry point: convert the entire parse tree.
  /// \return true on error
  bool convert();

  /// Enable mathematical (unbounded) integers instead of int64.
  void set_unbounded_ints(bool v)
  {
    unbounded_ints = v;
  }

  /// Set module resolver for import handling
  using module_resolver_t = std::function<const jsont *(const std::string &)>;
  void set_module_resolver(module_resolver_t resolver)
  {
    module_resolver = std::move(resolver);
  }

private:
  symbol_table_baset &symbol_table;
  const python_parse_treet &parse_tree;
  messaget log;
  std::string filename;

  /// Current function name (empty for top-level code)
  std::string current_function;

  /// Stack of enclosing function names (outermost first). A nested
  /// 'def' pushes the new function onto the stack. When a name is
  /// not found in the current function's scope, we fall back to
  /// looking it up in the enclosing scopes — this makes closure
  /// variables resolvable.
  std::vector<std::string> enclosing_functions;

  /// Map from Python function identifier (python::name) to the C
  /// intrinsic it should lower to. Populated by the @c_intrinsic('C-NAME')
  /// decorator in library stubs (see doc/architectural/
  /// python-module-support-plan.md, Step 3 / annotation-driven
  /// C-routing primitive). When a call's resolved symbol is in this
  /// map, the front-end emits a call to the named C function
  /// instead of calling the Python body. The C function is resolved
  /// at link-to-library time; the declared Python signature must
  /// match the C function's argument and return types.
  std::map<irep_idt, std::string> c_intrinsic_map;

  /// Optional constant-folder name, as specified by the
  /// ``@c_intrinsic('name', fold='op')`` decorator's ``fold``
  /// keyword. Populated alongside ``c_intrinsic_map``. When a
  /// call-site has all-constant arguments and a matching entry
  /// here, the front-end evaluates the known host-side op (one
  /// of std::sqrt, std::sin, …) and replaces the call with the
  /// resulting constant expression. Entries absent from this map
  /// (or absent from the recognised-op set in the implementation)
  /// behave exactly like a plain ``@c_intrinsic`` and route to C.
  std::map<irep_idt, std::string> c_intrinsic_fold_map;

  /// Optional domain-predicate name from
  /// ``@c_intrinsic('name', fold='op', domain='kind')``. When the
  /// call-site's argument is a constant that fails the domain
  /// predicate, the front-end raises Python ValueError (mimicking
  /// CPython's domain-check semantics for math functions). The
  /// 'kind' string corresponds to one of the predicates in
  /// ``math_function_domain``: 'nonneg', 'positive', 'gt_neg_one',
  /// 'abs_le_1', 'ge_1'. When absent, no domain check is emitted.
  std::map<irep_idt, std::string> c_intrinsic_domain_map;

  /// Optional return-range predicate name from
  /// ``@c_intrinsic('name', fold='op', range='kind')``. When the
  /// call-site's argument is symbolic (not foldable), the front-
  /// end returns a nondet value constrained by the named range
  /// predicate — mimicking the per-op constraints that
  /// CPython+CBMC's C math model enforces. The 'kind' string is
  /// one of: 'bound_pm_1' (|result| <= 1, for sin/cos),
  /// 'nonneg' (>= 0, for sqrt), 'positive' (> 0, for exp). When
  /// absent, the nondet return is unconstrained (sound over-
  /// approximation).
  std::map<irep_idt, std::string> c_intrinsic_range_map;

  /// Optional C-int bit-width from
  /// ``@c_intrinsic('name', int_width=32)``. When set, every
  /// Python ``int`` parameter or return of the decorated
  /// function is projected onto a C ``signed int`` of the
  /// specified width when constructing the C function
  /// signature. Without this annotation, Python ``int`` maps
  /// to ``signedbv 64`` which conflicts with CBMC's built-in
  /// declarations for C functions that take 32-bit ``int`` —
  /// notably the ctype predicates (isdigit, isalpha, ...) and
  /// the ``toupper`` / ``tolower`` / ``abs`` / ``atoi``
  /// families. Recognised widths: 32 and 64.
  std::map<irep_idt, int> c_intrinsic_int_width_map;

  /// Return the mathematical domain predicate for a single-argument
  /// math-module function. Returns nullopt when the function has no
  /// domain restriction. Used by the Option-4 domain-check logic
  /// that handles:
  ///   * constant in-domain argument  → fold to the exact value
  ///   * constant out-of-domain       → raise ValueError definitely
  ///   * non-constant argument        → guarded ValueError + nondet
  std::optional<exprt>
  math_function_domain(const std::string &func_name, const exprt &arg) const;

  /// Emit a Python-level ValueError. If ``in_domain`` is true at
  /// compile time, does nothing. If false/nil, raises definitely.
  /// Otherwise raises guardedly (if !in_domain: raise ValueError).
  void emit_value_error(const exprt &in_domain);

  /// Current class name (empty when not inside a class method)
  std::string current_class;

  /// Names declared 'global' in the current function
  std::set<std::string> global_names;
  /// PLR §7.13: names declared 'nonlocal' resolve to the nearest
  /// enclosing function's scope, not the module scope. Tracked per
  /// compilation (cleared when a function body begins).
  std::set<std::string> nonlocal_names;

  /// Whether to use mathematical integers instead of int64.
  bool unbounded_ints = false;
  bool processing_import = false;
  bool no_body_check = false; // suppress no-body-for-callee properties

  /// When true, promote front-end quiet-by-default diagnostics
  /// (Slice / Yield / YieldFrom, unresolved method / function /
  /// attribute access, subscript/for-in/'in' fallbacks) back to
  /// warning level. Intended for debugging spurious verification
  /// results where a silent over-approximation may be at fault.
  bool python_strict_warnings = false;

public:
  void set_no_body_check(bool v)
  {
    no_body_check = v;
  }

  void set_python_strict_warnings(bool v)
  {
    python_strict_warnings = v;
  }

  void set_python_lazy_stubs(bool v)
  {
    python_lazy_stubs = v;
  }

private:
  /// Lazy-stubs mode: imported modules get symbol-table entries
  /// (types, classes, function signatures) but no function
  /// bodies. Calls through returns nondet, no embedded
  /// assertions fire. Reduces memory/time blow-up when user
  /// code imports large stub trees.
  bool python_lazy_stubs = false;
  /// Emit a message about a front-end over-approximation. In the
  /// default mode the message goes to log.debug() so it is only
  /// visible at high verbosity; when --python-strict-warnings is
  /// set the same message is also emitted at log.warning() level.
  /// Used for cases where the front-end must fall back to a sound
  /// nondet over-approximation (unresolved method/function/name,
  /// attribute access on an opaque base, slice/yield expressions,
  /// subscript/for-in/'in' fallbacks).
  void log_overapprox(const std::string &msg)
  {
    log.debug() << msg << messaget::eom;
    if(python_strict_warnings)
      log.warning() << msg << messaget::eom;
  }
  // Deferred method bodies for on-demand conversion
  std::map<irep_idt, const jsont *>
    deferred_method_bodies; // true when inside process_imported_module
  module_resolver_t module_resolver;

  /// Process an imported module's AST to register its definitions
  void process_imported_module(
    const std::string &module_name,
    const jsont &module_ast);

  /// Map from class name to its struct type
  std::map<std::string, struct_typet> class_types;
  std::map<std::string, int> class_tag_ids;

  /// Map from class name to its base class names (for isinstance)
  std::map<std::string, std::vector<std::string>> class_bases;

  /// Map from variable name to function symbol (for lambda assignments)
  std::map<std::string, irep_idt> function_aliases;
  // Default parameter values evaluated at definition time
  // Maps (function_name, param_index) → default value expression
  std::map<std::pair<std::string, std::size_t>, exprt> default_values;
  // Functions that return lambdas: maps function name to lambda id
  std::map<std::string, irep_idt> lambda_returning_functions;
  // Bound methods: maps variable name → (method_id, self_expr)
  std::map<std::string, std::pair<irep_idt, exprt>> bound_methods;
  // Closure captures: maps qualified nested function name to list of
  // (outer_param_qualified_name, param_name, type) for captured variables
  std::
    map<std::string, std::vector<std::tuple<std::string, std::string, typet>>>
      closure_captures;
  // Track constant string values for string method evaluation
  std::map<irep_idt, std::string> string_constants;
  std::map<irep_idt, exprt> dict_literals; // track dict literal values
  std::map<irep_idt, exprt> list_literals; // track list literal values
  std::map<irep_idt, double> float_constants; // track float/int constant values
  std::optional<std::string> extract_string_value(const exprt &e) const;
  std::optional<double> try_eval_double(const exprt &e) const;

  /// Known imported module names (for `import math` style)
  std::set<std::string> imported_modules;
  std::set<std::string> generator_functions;

  /// Map from variable name (qualified) to its current versioned symbol.
  /// Used for fresh variable renaming when a variable changes type.
  std::map<std::string, irep_idt> variable_versions;

  /// Counter for generating unique version suffixes.
  std::map<std::string, unsigned> version_counters;

  /// Depth of if/else nesting (>0 means we're inside a branch).
  unsigned if_else_depth = 0;
  unsigned try_depth = 0;

  // --- AST node converters ---

  /// Convert a top-level module body into a code_blockt.
  code_blockt convert_module_body(const jsont &body);

  /// Convert a single statement node.
  codet convert_statement(const jsont &stmt);

  /// Convert an expression node to an exprt.
  exprt convert_expression(const jsont &expr);

  /// Convert a type annotation to a CBMC typet.
  typet convert_type_annotation(const jsont &annotation);

  // --- Statement converters ---
  codet convert_assign(const jsont &stmt);
  codet convert_ann_assign(const jsont &stmt);
  codet convert_aug_assign(const jsont &stmt);
  codet convert_assert(const jsont &stmt);

  /// Pending checks (div-by-zero, bounds) to be emitted before the
  /// next statement. Populated by expression converters, consumed by
  /// statement converters.
  std::vector<codet> pending_checks;
  codet convert_if(const jsont &stmt);
  codet convert_while(const jsont &stmt);
  codet convert_for(const jsont &stmt);
  codet convert_return(const jsont &stmt);
  codet convert_function_def(const jsont &stmt);
  codet convert_class_def(const jsont &stmt);
  codet convert_expr_stmt(const jsont &stmt);
  codet convert_break();
  codet convert_continue();
  codet convert_pass();
  codet convert_raise(const jsont &stmt);
  codet convert_with(const jsont &stmt);
  codet convert_try(const jsont &stmt);

  // --- Expression converters ---
  exprt convert_name(const jsont &expr);
  exprt convert_constant(const jsont &expr);
  exprt convert_bin_op(const jsont &expr);
  exprt convert_unary_op(const jsont &expr);
  exprt convert_bool_op(const jsont &expr);
  exprt convert_compare(const jsont &expr);
  exprt convert_call(const jsont &expr);
  exprt convert_if_exp(const jsont &expr);
  exprt convert_subscript(const jsont &expr);
  exprt convert_tuple(const jsont &expr);
  exprt convert_list(const jsont &expr);
  exprt convert_attribute(const jsont &expr);
  exprt convert_dict(const jsont &expr);
  exprt convert_list_comp(const jsont &expr);
  exprt convert_dict_comp(const jsont &expr);
  exprt convert_lambda(const jsont &expr);

  /// Counter for generating unique lambda names.
  unsigned lambda_counter = 0;

  // --- Helpers ---

  /// Get the source location from a JSON AST node.
  source_locationt get_location(const jsont &node) const;

  /// Get a JSON object member, returning null_json if not found.
  const jsont &json_member(const jsont &obj, const std::string &key) const;

  /// Get a string value from a JSON node.
  std::string json_string(const jsont &node) const;

  /// Get an integer value from a JSON node.
  long long json_integer(const jsont &node) const;

  /// Check if a JSON node has a specific _type field.
  bool is_node_type(const jsont &node, const std::string &type_name) const;

  /// Safely cast a jsont to json_arrayt, returning an empty array if not array.
  const json_arrayt &as_array(const jsont &node) const;

  /// Add a property check (assertion) to pending_checks.
  void add_check(
    exprt condition,
    const std::string &property_class,
    const std::string &comment,
    const source_locationt &loc);

  /// Get the qualified symbol name for a variable, respecting
  /// function scope and 'global' declarations.
  std::string qualify_name(const std::string &name) const;

  /// Materialise a Python string literal as a refined-string
  /// struct whose content pointer refers to a persistent,
  /// static-lifetime array symbol. The backing array is one
  /// byte longer than the logical string and carries a trailing
  /// NUL, so the same pointer can be handed to C as a
  /// null-terminated ``char *`` — unlike an inline
  /// address_of(array_literal[0]), whose temporary storage is
  /// "dead" at call time from CBMC's safety-check perspective.
  /// Literals are interned by content to avoid emitting a new
  /// symbol for every identical literal.
  exprt build_string_literal(const std::string &s);

  /// Build a Python string value for the active back-end.
  /// Dispatches on ``python_string_kind``:
  ///   * refined: returns a refined-string struct (today's
  ///     shape — same as the deprecated build_string_struct).
  ///   * smt_string: emits an smt_string_constant_exprt
  ///     whose convert_expr lowering produces the SMT-LIB
  ///     literal "...". Currently a placeholder; the full
  ///     lowering lands in a follow-up PR.
  ///
  /// This is the single producer-side entry point for string
  /// literals in the front-end. See
  /// doc/architectural/python-string-phase2-backend-abstraction.md.
  exprt python_string_literal(const std::string &s);

  /// Emit a guarded ValueError for a symbolic math argument
  /// whose domain predicate rejects it, plus a fresh nondet
  /// return symbol constrained by the named range predicate.
  /// Shared by the decorator-driven path and the attribute-
  /// style ``math.X(...)`` handler. Returns the nondet symbol.
  exprt emit_math_intrinsic_nondet(
    const std::string &domain_kind,
    const std::string &range_kind,
    const exprt &arg,
    const source_locationt &loc);

  /// Return the CBMC type used for Python int.
  typet python_int_type() const
  {
    if(unbounded_ints)
      return integer_typet{};
    return signedbv_typet{64};
  }

  /// Unwrap a tagged-union value to a specific type, or return it as-is
  /// if it's already a concrete type.
  exprt unwrap_value(const exprt &e, const typet &target_type);

  /// Wrap a concrete typed value into a tagged-union value.
  exprt wrap_value(const exprt &e);

  /// Safe typecast: handles tagged unions, struct-to-scalar, and other
  /// cases that would crash with a raw typecast_exprt.
  exprt safe_typecast(const exprt &e, const typet &target);

  /// Safe zero: returns from_integer(0, type) for numeric types,
  /// or a nondet value for struct/other types.
  exprt safe_zero(const typet &type) const;

  /// Compute exception type hash for a given type name.
  /// Uses class_tag_ids if available, else sum of ASCII values.
  long exception_type_hash(const std::string &type_name) const;
};

#endif // CPROVER_PYTHON_PYTHON_CONVERTER_H
