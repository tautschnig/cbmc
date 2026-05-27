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

  /// Set a resolver that maps module names to file paths. Used
  /// by the converter to attribute source locations of imported
  /// stub code to the stub file rather than the main source.
  using module_path_resolver_t =
    std::function<std::string(const std::string &)>;
  void set_module_path_resolver(module_path_resolver_t resolver)
  {
    module_path_resolver = std::move(resolver);
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

  void set_python_no_exception_checks(bool v)
  {
    python_no_exception_checks = v;
  }

  void set_python_required_kwarg_checks(bool v)
  {
    python_required_kwarg_checks = v;
  }

  void set_python_check_typeddict_fields(bool v)
  {
    python_check_typeddict_fields = v;
  }

  void set_python_check_annotations(bool v)
  {
    python_check_annotations = v;
  }

  void set_python_check_any_arg_attrs(bool v)
  {
    python_check_any_arg_attrs = v;
  }

private:
  /// Lazy-stubs mode: imported modules get symbol-table entries
  /// (types, classes, function signatures) but no function
  /// bodies. Calls through returns nondet, no embedded
  /// assertions fire. Reduces memory/time blow-up when user
  /// code imports large stub trees.
  bool python_lazy_stubs = false;
  /// Suppress emission of "uncaught exception" property checks.
  /// When set, module-level statements don't get the trailing
  /// assert(!__exception_active). Useful for benchmark suites
  /// whose bug-detection criterion is assertion-failure only,
  /// not exception-propagation.
  bool python_no_exception_checks = false;
  /// Emit key-presence assertions for Required fields of
  /// Unpack[TypedDict] kwargs in imported stub methods.
  /// Catches 'missing required argument' bugs at the stub-skip
  /// point. Off by default because caller-side **dict spread
  /// isn't fully modelled — spread-based callers produce FPs.
  /// Enable for benchmark suites that use only explicit
  /// 'key=value' kwargs.
  bool python_required_kwarg_checks = false;
  /// Emit 'type-error' property checks at PEP 448 dict-spread
  /// call sites when the spread dict's literal value for a key
  /// has a static type that's incompatible with the
  /// corresponding TypedDict field's declared type. Off by
  /// default; opt-in via --python-check-typeddict-fields.
  bool python_check_typeddict_fields = false;
  /// Emit 'annotation-mismatch' property checks at variable,
  /// parameter, and return annotation boundaries when the
  /// value's statically-known type is obviously incompatible
  /// with the declared annotation. Catches unsoundness from
  /// annotation-trust (PLR §3.3 — annotations aren't
  /// runtime-enforced). Off by default because our frontend's
  /// default nondet-int return for unresolved calls produces
  /// mismatches with class-typed annotations — enable only
  /// for user-code auditing, not stub-heavy code.
  bool python_check_annotations = false;
  /// When true, emit attribute-error properties at call sites
  /// where the callee's parameter is `Any`-typed and the
  /// caller's argument has a known concrete class type that
  /// doesn't have an attribute referenced via `param.X` in the
  /// callee's body. Detects the cross-function Any-erasure
  /// pattern (e.g. `bedrock_data_automation_example`).
  /// Off-by-default (opt-in via --python-check-any-arg-attrs).
  bool python_check_any_arg_attrs = false;
  /// Map from `python::<func-id>::<param-name>` → set of
  /// attribute names referenced via `param.<name>` (or
  /// `param.<name>(...)`) in the function body. Populated by
  /// `collect_param_attribute_uses` during `convert_function_def`
  /// and consulted at call sites when
  /// `python_check_any_arg_attrs` is on.
  std::map<irep_idt, std::set<std::string>> function_param_attr_uses;
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
  module_path_resolver_t module_path_resolver;

  /// Process an imported module's AST to register its definitions
  void process_imported_module(
    const std::string &module_name,
    const jsont &module_ast);

  /// Map from class name to its struct type
  std::map<std::string, struct_typet> class_types;
  std::map<std::string, int> class_tag_ids;

  /// Map from class name to its base class names (for isinstance)
  std::map<std::string, std::vector<std::string>> class_bases;
  /// PLR §3.3.2.1 C3 linearization. The MRO for each class,
  /// starting with the class itself. Populated on ClassDef by
  /// compute_c3_mro().
  std::map<std::string, std::vector<std::string>> class_mro;
  /// Per-class set of method names that are declared with
  /// @property. Attribute reads of these names call the method
  /// with self as the single argument (PLR §3.3.2).
  std::map<std::string, std::set<std::string>> class_property_methods;
  /// All method names declared on a class (regardless of whether
  /// they have been converted yet). Populated in convert_class_def
  /// before any method body is converted, so forward-reference
  /// calls (`self.foo()` inside `__init__` where `foo` appears
  /// later in the class body) can be distinguished from genuinely
  /// missing methods.
  std::map<std::string, std::set<std::string>> class_declared_methods;
  /// The class whose method call initiated the current super()
  /// dispatch. Set by the call site (e.g. when D() is called,
  /// set to "D"); nested super() inlining preserves it. Empty
  /// outside any dispatch.
  std::string mro_root_class;

  /// Map from variable name to function symbol (for lambda assignments)
  std::map<std::string, irep_idt> function_aliases;
  /// PLR §8.7: index of the *args parameter for each function that
  /// has one. Stored explicitly because closure captures are appended
  /// after *args, so its position isn't always last.
  std::map<irep_idt, std::size_t> function_vararg_index;
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
  /// Map from function name to the set of constant keys in
  /// its return-statement dict literal. Used by convert_assign
  /// to propagate dict_literals across function-call boundaries:
  /// if the callee returns {'managed': ..., 'inline': ...},
  /// the caller's receiving variable can trust those keys.
  std::map<std::string, std::set<std::string>> function_returned_dict_keys;
  /// Map from function name to a literal struct value the
  /// function unconditionally returns. Stronger than
  /// function_returned_dict_keys: when a function body is
  /// effectively `return <constant struct>`, we cache the
  /// full struct (keys *and* values, or list/tuple elements)
  /// so call sites of the form `x = func()` can populate the
  /// caller's dict_literals/list_literals/tuple_literals with
  /// the full literal. Closes the dict-of-list / tuple-return
  /// constant-fold gap. Populated by convert_return; consumed
  /// by convert_assign.
  std::map<std::string, exprt> function_returned_literal;
  /// Per-function count of return statements seen during
  /// conversion. Only when this is exactly 1 do we trust the
  /// cached literal in function_returned_literal: a function
  /// with multiple return paths could yield different values,
  /// so the cached literal isn't safe to use unconditionally.
  std::map<std::string, std::size_t> function_return_count;
  std::map<irep_idt, exprt> list_literals; // track list literal values
  /// Track tuple literal values keyed by symbol identifier. Same
  /// purpose as list_literals: lets the constant-fold path in
  /// convert_call resolve `min(t)`/`max(t)`/etc. when `t` was
  /// assigned an inline tuple. Populated from convert_assign on
  /// any `name = (...)` whose RHS is a python_tuple struct_exprt.
  std::map<irep_idt, exprt> tuple_literals;
  /// PLR §6.5: Track python_complex struct literals so the
  /// constant-fold path for `complex_var ** N` and similar
  /// can recover the (real, imag) components from a symbol
  /// assigned via 'z = complex(re, im)'.
  std::map<irep_idt, exprt> complex_literals;
  std::map<irep_idt, double> float_constants; // track float/int constant values

  /// PLR control-flow correctness: per-branch snapshot + merge for
  /// conversion-time constant-tracking maps.
  ///
  /// The frontend constant-folds at conversion time using the maps
  /// above (string_constants, dict_literals, list_literals,
  /// tuple_literals, float_constants). When an assignment occurs
  /// inside an if/else (or match/case, or try/except), the writes
  /// from one arm leak into the other arm's processing because the
  /// converter walks both arms sequentially. Without merge logic,
  /// the final state has the LAST-PROCESSED arm's writes, which
  /// downstream reads then incorrectly treat as known.
  ///
  /// Solution: snapshot the maps before any branch. Process each
  /// arm independently, capturing per-arm post-states. After all
  /// arms are processed, merge: for every key that appears in
  /// any arm's post-state, retain the value only if all arms
  /// agree (same value for keys all arms set, OR all arms left
  /// unchanged so the snapshot value still holds). Keys where
  /// arms disagree are dropped, falling through to runtime SSA.
  struct tracking_snapshott
  {
    std::map<irep_idt, std::string> string_constants;
    std::map<irep_idt, exprt> dict_literals;
    std::map<irep_idt, exprt> list_literals;
    std::map<irep_idt, exprt> tuple_literals;
    std::map<irep_idt, double> float_constants;
    std::map<irep_idt, irep_idt> alias_targets;
    // function_aliases and bound_methods are intentionally NOT
    // snapshot/merged: they record one-way name → callable
    // mappings whose runtime dispatch is needed for any program
    // that reassigns a callable in a branch (e.g. `if cond: h = f
    // else: h = g; h(...)`). Merge would drop the entries and
    // emit "no body for callee h", regressing the common case
    // where the condition is constant or where the rebind is
    // semantically uniform. We accept the residual path-
    // insensitivity for these two maps as a known trade-off.
  };

  /// Capture the current state of all conversion-time tracking
  /// maps. O(N) in the size of all maps.
  tracking_snapshott snapshot_tracking() const;

  /// Replace all conversion-time tracking maps with the snapshot's
  /// values. Used between branches to revert the writes from the
  /// previous branch before processing the next one.
  void restore_tracking(const tracking_snapshott &snap);

  /// Merge two arm-states into the live tracking maps. For each
  /// key appearing in either arm, retain the entry only if both
  /// arms agree on its value (same bytes for *_constants, same
  /// expression structure for *_literals). Disagreements are
  /// dropped from the live maps. Keys not present in either arm
  /// are also absent from the merged result.
  ///
  /// The order is: snapshot pre-branches captured, then apply
  /// snapshot, process arm0, capture state0, restore snapshot,
  /// process arm1, capture state1, then call merge_tracking with
  /// state0 and state1 to overwrite the live maps.
  void merge_tracking(
    const tracking_snapshott &state0,
    const tracking_snapshott &state1);

  /// PLR §8.7: leaf functions whose body is a single \`return <constant>\`
  /// statement have a known compile-time return value. Recording it here
  /// lets the assignment site \`c = f()\` propagate the constant through
  /// downstream constant-folding (e.g. chr(c) at the call site folds to
  /// the corresponding string when c is bound from such a leaf function).
  /// Populated at the end of convert_function_def by inspecting the AST
  /// body shape, and queried by try_eval_double when the expression
  /// being evaluated is a function-call side-effect.
  std::map<irep_idt, double> function_return_constants;

  /// Module-level globals registered by pass 0's plain-Assign
  /// pre-pass with a tentative placeholder type. Pass 0 has no
  /// access to the converted RHS expression, so it defaults to
  /// python_int_type() when the RHS isn't a Constant or List
  /// literal. The actual symbol type is then refined in pass 2
  /// when convert_assign sees the converted RHS: if the symbol
  /// is in this set and the RHS type doesn't match, replace the
  /// symbol's type rather than casting (which would be lossy
  /// for e.g. `x = math.inf`, where casting +inf into a 64-bit
  /// signed int erases the infinity).
  ///
  /// AnnAssign-registered symbols are NOT placed in this set —
  /// the user has explicitly declared a type, and pass 2 should
  /// continue to cast the RHS to honour that annotation.
  std::set<irep_idt> unannotated_globals;

  /// PLR §3.1: names rebound to a mutable container do not copy the
  /// container; they bind to the same object. To model that here, when
  /// `b: list = a` (or the unannotated `b = a`) is converted with the
  /// RHS resolving to another list/dict-typed symbol, the LHS symbol
  /// is promoted to a pointer-to-struct and bound to `address_of(a)`.
  /// `convert_name` then auto-dereferences the LHS at every read site,
  /// so member access (`b[0]`, `b.length`) and method calls
  /// (`b.append(v)`, `b.pop()`) operate through the deref'd pointer
  /// and mutations propagate to the original `a`.
  ///
  /// `alias_targets[qualified_name(b)] = qualified_name(a)` records the
  /// chain (transitive aliases store the FINAL target, never another
  /// alias, so `c = b = a` collapses to `alias_targets[c] = a`). Used
  /// by the `is`/`is not` handler to compare alias identity even when
  /// the operands are auto-dereferenced symbol_exprt's at use sites.
  std::map<irep_idt, irep_idt> alias_targets;

  /// PLR §3.1: per-scope set of qualified names that escape into a
  /// container literal (i.e. appear as a Name element of a List/Dict
  /// literal). Such names get their storage promoted to
  /// `list[python_value]` (or stay `dict[str, python_value]`) so that
  /// the tagged-union deref-cast in `python_value_list` is type-
  /// correct, enabling sound nested-mutable semantics:
  ///
  ///     inner: list = [1, 2]
  ///     outer = [inner]      # outer.data[0] = pv(LIST, &inner)
  ///     outer[0][0] = 99     # writes through to inner.data[0]
  ///     assert inner[0] == 99
  ///
  /// Populated by collect_escaped_mutables (a pre-scan over the
  /// module body / function body before convert_module_body runs).
  std::set<irep_idt> escaped_mutables;
  void collect_escaped_mutables(const jsont &body);
  std::optional<std::string> extract_string_value(const exprt &e) const;

  /// PLR §8.2 / §8.3: invalidate conversion-time constant tracking
  /// for variables assigned inside a loop body. The frontend folds
  /// `string_constants[x]`, `float_constants[x]`, etc. when converting
  /// a statement at parse time, but loop iterations re-bind those
  /// variables to values the converter doesn't know. Without
  /// invalidation, the body's expressions get folded against the
  /// stale pre-loop value, producing a body that's unsound for any
  /// iteration past the first. Call this BEFORE converting a loop's
  /// body to remove tracked constants for assignment / for-target /
  /// AugAssign targets that appear anywhere in the body.
  void invalidate_loop_writes(const jsont &body);

  /// PLR §6.2.9: when assigning `g = gen()` or `g: T = gen()`
  /// where `gen` is a generator function, allocate the hidden
  /// cursor symbol `__cursor_<g>` and register it in
  /// `generator_cursors[g]`. Returns the initialisation code
  /// (`__cursor_<g> = 0`) that the caller should append after
  /// the symbol assignment, or `code_skipt` when the RHS is not
  /// a recognised generator-function call.
  codet allocate_generator_cursor(
    const irep_idt &symbol_id,
    const jsont &value,
    const source_locationt &loc);

  /// Best-effort static category of an expression node directly
  /// from the Python AST (i.e. before any safe_typecast erases
  /// its original type). Returns one of {"str","int","float",
  /// "bool","list","dict","set","tuple","none","bytes"} when
  /// determinable from a Constant, Dict, List, Set, or Tuple
  /// AST node, or an empty string otherwise. Used by
  /// --python-check-typeddict-fields to decide whether a
  /// kwarg passed via **dict_spread has a static category that
  /// disagrees with the corresponding TypedDict field's
  /// declared category.
  std::string ast_value_category(const jsont &node) const;
  std::optional<double> try_eval_double(const exprt &e) const;

  /// Known imported module names (for `import math` style)
  std::set<std::string> imported_modules;

  /// PLR §8.5: collections module imports. Maps the binding
  /// name (asname after 'from collections import X as Y' / bare
  /// X / fully-qualified collections.X) to the original
  /// collections symbol name ("defaultdict", "Counter", ...).
  /// Lets convert_call route the call to the right
  /// special-case constructor regardless of how the user
  /// imported the symbol.
  std::map<std::string, std::string> collections_imports;

  /// PLR §8.5: out-of-band hint from convert_call to
  /// convert_assign. When convert_call sees a defaultdict /
  /// Counter constructor, it stashes the factory name here so
  /// convert_assign (which has access to the LHS target name)
  /// can register the new variable in defaultdict_factories.
  /// Cleared after each top-level assignment.
  std::string pending_defaultdict_factory;
  /// Names whose import could not be resolved. Populated when
  /// module_resolver returns nullptr for 'import X' or
  /// 'from Y import ...'. Calls to these names should not
  /// emit the no-body-for-callee property — the tool cannot
  /// be sound about their bodies but the user typically
  /// intends this as a benign over-approximation rather than
  /// a bug.
  std::set<std::string> unresolved_imports;
  /// Map from TypedDict class name to its required keys.
  /// Populated when processing imported stubs for statements
  /// like 'InputT = TypedDict("InputT", {"K": Required[T], ...})'
  /// or 'class InputT(TypedDict): K: Required[T]'.
  /// Used by convert_class_def when it skips a stub method body
  /// (auto-detected PySpec Unpack pattern): instead of omitting
  /// all preconditions, we emit key-presence checks for each
  /// required kwarg, catching 'missing required argument' bugs
  /// without triggering the regex/length assertions that
  /// overwhelm the string refinement solver.
  std::map<std::string, std::vector<std::string>> typed_dict_required;

  /// Map from TypedDict name to a per-field declared type. Each
  /// field is recorded as one of {"str", "int", "float", "bool",
  /// "list", "dict", "set", "bytes"} — the underlying Python
  /// scalar/collection categories we can statically test against
  /// at call sites. Fields whose declared type isn't one of
  /// these (e.g. another TypedDict, Union, Literal, ...) are
  /// omitted, since we can't soundly enforce them with a single
  /// category check.
  ///
  /// Used by the --python-check-typeddict-fields opt-in: at each
  /// kwarg passed to a stub method whose **kwargs is annotated
  /// Unpack[TypedDictName], the corresponding entry here is
  /// consulted. When the passed value is a constant whose static
  /// category disagrees with the declared one (e.g. None passed
  /// where 'str' is expected), a 'type-error' property is
  /// emitted at the call site.
  std::map<std::string, std::map<std::string, std::string>>
    typed_dict_field_types;

  /// Map from fully-qualified method symbol id (e.g.
  /// "python::S3::copy_object") to the TypedDict name that
  /// annotates its **kwargs parameter via Unpack[...].
  /// Populated in convert_class_def alongside the existing
  /// Tier 1B Unpack detection. Used by call sites to look up
  /// the per-field expected types for type-checking values
  /// passed via PEP 448 dict spread.
  std::map<irep_idt, std::string> method_kwargs_unpack;

  /// Per-dict per-key static value category derived from the
  /// Python AST at assignment time (before any typecast in the
  /// converted exprt). Populated when a `Dict(...)` literal is
  /// assigned to a Name. Used by --python-check-typeddict-fields
  /// at PEP 448 spread call sites: looking at the converted
  /// dict's value array isn't reliable because heterogeneous
  /// values are unified via safe_typecast (which falls through
  /// to a nondet of the target type for incompatible source
  /// types, erasing the original category). The AST is the
  /// only place the original types survive verbatim.
  ///
  /// Categories are the same set used by typed_dict_field_types
  /// ("str", "int", "float", "bool", "list", "dict", "set",
  /// "tuple", "none").
  std::map<irep_idt, std::map<std::string, std::string>>
    dict_literal_value_categories;

  /// Sibling of dict_literal_value_categories tracking per-key
  /// constant string VALUES from the original Python AST. Only
  /// populated for keys whose AST value is a Constant(str). Used
  /// by the call-site regex-no-match check (stage 1 of the re
  /// precision plan): when a Pattern.search subject is read out
  /// via dict_literal["key"] and we know it's the empty string
  /// (or any constant the regex can't match), we can emit a
  /// regex-no-match property at the call site without depending
  /// on solver-level reasoning.
  std::map<irep_idt, std::map<std::string, std::string>>
    dict_literal_value_string_consts;

  /// PLR §3.1, §3.2: per-key runtime-value overrides for typed
  /// dicts. When `d: dict[K, V] = {...}; d[k] = v` stores a value
  /// `v` whose runtime type doesn't match the declared `V`, the
  /// dict's storage array (typed `V[]`) coerces it via
  /// safe_typecast, losing the actual value. To keep
  /// PLR-correct read-back semantics — `d[k]` returns the
  /// runtime value, not a nondet of the declared type — we
  /// record the original RHS expression here keyed by the
  /// stringified constant key. The dict subscript read path
  /// returns the override directly when present, so
  /// `isinstance(d[k], V)` correctly reflects the stored
  /// value's actual type.
  ///
  /// Cleared on any non-constant subscript-assign to the same
  /// dict (because subsequent constant-key reads would no
  /// longer be sound).
  std::map<irep_idt, std::map<std::string, exprt>> dict_runtime_value_overrides;

  /// PLR §6.10.2: per-symbol marker that a name has been bound
  /// to a type object (e.g. `x = int`). Used by isinstance(x,
  /// type) to return True without inspecting the symbol's
  /// runtime int value (which is the type-tag rather than a
  /// real instance). Populated in convert_assign when the RHS
  /// is a Name resolving to a built-in type name or a
  /// registered class name.
  std::set<irep_idt> name_holds_type_binding;

  /// Stage 1 of the re-precision plan, second part: recorded
  /// regex assertions found inside class method bodies of the
  /// shape
  ///     assert compile("...").search(kwargs[K1][K2]...) is not None
  /// (and search/match/fullmatch / re.<method> variants).
  ///
  /// Keyed by the qualified method symbol id. Each entry is the
  /// list of (kwarg_path, pattern) pairs recovered from the
  /// method body. At each user call site, when a kwarg value is
  /// a Dict literal that nests deep enough to resolve the
  /// kwarg_path to a constant string subject, we can emit a
  /// regex-no-match property at the call site without having to
  /// propagate the dict contents through the function-call
  /// boundary at goto time.
  struct stub_regex_assertt
  {
    std::vector<std::string> kwarg_path;
    std::string pattern;
  };
  std::map<irep_idt, std::vector<stub_regex_assertt>> method_regex_asserts;
  /// Set of function names (qualified) whose 'returns' annotation
  /// was explicitly provided. Used by convert_return to emit an
  /// annotation-mismatch property only when the function HAS a
  /// declared return annotation — otherwise our inferred default
  /// (int) produces spurious mismatches.
  std::set<std::string> annotated_return_functions;
  /// Path-sensitive dict-key tracking: after an 'if K not in D:
  /// D[K] = default' idiom, K is guaranteed to be in D (either
  /// added by the body or already present). We record the
  /// (dict symbol, key AST string) pairs so subsequent
  /// D[K'] subscript reads can skip the KeyError check when
  /// K' structurally matches a guaranteed key.
  std::map<irep_idt, std::set<std::string>> dict_guaranteed_keys;

  /// PLR §8.5: collections.defaultdict(factory). Tracks dicts
  /// constructed via 'collections.defaultdict(F)' / 'Counter()'
  /// — for these, missing-key reads return F() (the factory's
  /// zero value: 0 for int, "" for str, [] for list, etc.)
  /// instead of raising KeyError. Map: dict-symbol-id →
  /// factory-type-name (e.g. "int", "str", "list", "Counter",
  /// or "" for defaultdict(None) which falls back to plain-dict
  /// behaviour).
  std::map<irep_idt, std::string> defaultdict_factories;

  /// Path-sensitive lower bounds on list lengths active in the
  /// current expression scope. Populated by the short-circuiting
  /// 'and' idiom recogniser in convert_bool_op when the first
  /// operand is `len(L) >= N` or `len(L) > N` (and similar
  /// reversed forms). Consulted in convert_subscript on a list
  /// to discharge the IndexError property when the index is a
  /// non-negative constant smaller than the recorded bound.
  /// Stored as (qualified-symbol-id) -> minimum-known-length.
  std::map<irep_idt, mp_integer> list_min_lengths;
  /// Path-sensitive lower bounds on string lengths. Same shape
  /// as `list_min_lengths`, populated by the `if s:` truthiness
  /// recogniser in `convert_if` (a non-empty string is truthy in
  /// Python, so the body sees `len(s) >= 1`). Consulted in
  /// `convert_subscript` for `python_string_type` values to
  /// discharge the IndexError property when the index is a
  /// non-negative constant smaller than the recorded bound.
  std::map<irep_idt, mp_integer> string_min_lengths;
  std::set<std::string> generator_functions;

  /// PLR §6.2.9: Map from a generator-instance symbol id (e.g. the
  /// `g` in `g = gen()` where `gen` is a generator function) to the
  /// id of its hidden cursor symbol (`__cursor_<g>`). The cursor
  /// tracks how many `next(g)` calls have been issued. Allocated
  /// in convert_assign / convert_ann_assign when the RHS is a call
  /// to a function in `generator_functions`. Consulted by next()
  /// in convert_call to advance the cursor and raise StopIteration
  /// at the end of the eager-yield list.
  std::map<irep_idt, irep_idt> generator_cursors;

  /// Map from variable name (qualified) to its current versioned symbol.
  /// Used for fresh variable renaming when a variable changes type.
  std::map<std::string, irep_idt> variable_versions;

  /// Counter for generating unique version suffixes.
  std::map<std::string, unsigned> version_counters;

  /// Depth of if/else nesting (>0 means we're inside a branch).
  unsigned if_else_depth = 0;
  unsigned try_depth = 0;
  /// Depth of enclosing for/while loops at the current AST node.
  /// Used by string-handling helpers to decide whether to havoc
  /// SSA outputs of cprover_string_*_func intrinsics. Outside any
  /// loop the same call site never repeats, so havocing is
  /// unnecessary (and would cost an extra pointer object per
  /// call).
  unsigned loop_depth = 0;

  /// Stack of active exception handlers. Each frame is the set of
  /// exception class names caught by one enclosing try/except. An
  /// exception class X is considered caught if X or a catch-all
  /// ("Exception", "BaseException", "") is present in any frame.
  /// Used for definitively-unhandled-exception detection (e.g.
  /// missing method calls).
  std::vector<std::set<std::string>> active_exception_handlers;

  /// Return true if `exc_class` would be caught by an active
  /// enclosing except handler. Only EXACT class-name matches
  /// suppress the missing-method assertion; generic catch-alls
  /// like 'except Exception:' or bare 'except:' do NOT suppress
  /// it because those handlers are typically used for last-resort
  /// recovery, not to mask statically-known wrong method names.
  /// (PLR semantics: such a call IS an AttributeError; the catch
  /// just hides the symptom while the bug remains.)
  bool exception_is_caught(const std::string &exc_class) const
  {
    for(const auto &frame : active_exception_handlers)
    {
      if(frame.count(exc_class) > 0)
        return true;
    }
    return false;
  }

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

  /// PLR §8.2 / §8.3: for-else / while-else support.
  /// Stack of break-flag symbol ids, pushed when entering a
  /// loop whose orelse is non-empty and popped on exit. Each
  /// break statement nested inside sets the top-of-stack flag
  /// to true before breaking. The else clause runs when the
  /// flag remains false after the loop.
  std::vector<irep_idt> loop_break_flags;

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

  /// Dispatch the verification-primitive call group (handled by
  /// python_converter_call_nondet.cpp): nondet_int / nondet_float /
  /// nondet_bool / nondet_str / nondet_list / nondet_dict /
  /// nondet_complex / their __VERIFIER_nondet_* aliases / randint /
  /// the assume family / and the regex frontend hooks
  /// __cbmc_re_{match,search,fullmatch}. Returns nullopt if
  /// func_name doesn't match any of the recognised primitives so
  /// convert_call can fall through to the next dispatch group.
  std::optional<exprt> try_nondet_call(
    const jsont &expr,
    const std::string &func_name,
    const jsont &args);

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

  /// PLR semantic correctness: detect cases where a value's
  /// statically-known type is incompatible with a declared
  /// annotation. Python doesn't enforce annotations at runtime,
  /// but when a mismatch exists, downstream operations (e.g.
  /// 'str_val + int_val' after 'x: int = "hello"') will raise
  /// TypeError at runtime. Since our value-tracking trusts the
  /// annotation, we miss those TypeErrors — adding an explicit
  /// property at the annotation site restores soundness.
  /// Returns true when 'actual' and 'declared' are concrete
  /// types in mutually-exclusive categories (e.g. str vs int).
  /// Tagged-union (Any) on either side returns false (duck-typed).
  bool annotation_types_incompatible(const typet &declared, const typet &actual)
    const;

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

  /// PLR §4.1 (Truth Value Testing): return a `bool_typet`-typed
  /// expression that is true iff `e` is "truthy" in Python.
  ///
  /// Per PLR / object.__bool__ docs, the following are FALSE:
  ///   * False, None, 0 (any numeric zero, including 0.0 and -0.0)
  ///   * Empty sequence: "", [], (), b""
  ///   * Empty mapping: {}, set()
  ///   * Class instances whose `__bool__` returns False, or whose
  ///     `__len__` returns 0 when `__bool__` is absent.
  ///
  /// Everything else (including NaN!) is truthy. The current
  /// implementation handles the primitive and built-in collection
  /// cases; class-instance dispatch through __bool__/__len__ is
  /// done by the caller via dunder-method lookup at call sites
  /// (so a single helper doesn't need access to symbol_table state).
  exprt python_truthiness(const exprt &e);

  /// PLR §3.1: rebuild a list-struct expression so its element type
  /// is `python_value`. Each existing data element is `wrap_value`'d
  /// individually. Used when promoting an escaped mutable's storage
  /// (so subsequent `python_value_list` deref-casts read the right
  /// memory layout). Input must be a python_list-typed exprt; result
  /// has type `python_list_type(python_value_type())`.
  exprt rebuild_list_as_pv(const exprt &list_expr);

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
