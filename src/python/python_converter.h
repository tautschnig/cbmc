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
#include "python_types.h"

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
    python_unbounded_ints_flag() = v;
  }

  /// SPIKE (--python-ref-mutables): model anonymous mutable list elements with
  /// reference semantics (heap-allocate per instance + alias by pointer)
  /// instead of by value. Lets extraction/aliasing/multi-instance be precise
  /// rather than guarded. List literals only for now (see
  /// doc/python-frontend-reference-semantics-spike.md).
  bool ref_mutables = false;
  void set_ref_mutables(bool v)
  {
    ref_mutables = v;
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

  /// icontract postcondition expressions for the function we
  /// are currently converting. Populated by convert_function_def
  /// when @icontract.ensure decorators are recognised; consumed
  /// by convert_return to emit the assertion before each return
  /// statement, and by convert_function_def itself for the
  /// implicit fall-through return. Save/restored across nested
  /// function definitions so each function sees its own
  /// postconditions.
  std::vector<exprt> active_ensures;

  /// icontract @snapshot bindings active while translating an
  /// ensure lambda body. Maps snapshot name → captured
  /// expression (typically a symbol_exprt for the synthesised
  /// per-function snapshot variable). When convert_attribute
  /// sees an Attribute node with value = Name("OLD") and the
  /// attr is in this map, it returns the captured expression
  /// directly instead of going through the normal
  /// attribute-resolution path. Save/restored across nested
  /// function definitions.
  std::map<std::string, exprt> active_old_snapshots;

  /// icontract Phase 7: per-class @icontract.invariant lambda
  /// AST pointers, keyed by class name. Populated by
  /// convert_class_def as each class is processed. Used by
  /// the per-method contract emission to compose the class's
  /// own invariants with those of every base class (Liskov:
  /// child invariants merge with parent's via AND).
  std::map<std::string, std::vector<const jsont *>> class_invariant_lambdas;

  /// icontract Phase 7: per-class per-method @require lambda
  /// AST pointers. class_method_require_lambdas[Cls][m] is
  /// the list of @require lambdas declared on Cls.m. Used by
  /// the inheritance composition step: when a subclass overrides
  /// a base-class method, the effective precondition weakens to
  /// `parent_pre OR child_pre` per Liskov.
  std::map<std::string, std::map<std::string, std::vector<const jsont *>>>
    class_method_require_lambdas;

  /// icontract Phase 7: per-class per-method @ensure lambda
  /// AST pointers. Mirror of class_method_require_lambdas.
  /// Effective postcondition strengthens to
  /// `parent_post AND child_post` per Liskov.
  std::map<std::string, std::map<std::string, std::vector<const jsont *>>>
    class_method_ensure_lambdas;

  /// Stack of enclosing function names (outermost first). A nested
  /// 'def' pushes the new function onto the stack. When a name is
  /// not found in the current function's scope, we fall back to
  /// looking it up in the enclosing scopes — this makes closure
  /// variables resolvable.
  std::vector<std::string> enclosing_functions;

  /// Map from Python function identifier (python::name) to the C
  /// intrinsic it should lower to. Populated by the @c_intrinsic('C-NAME')
  /// decorator in library stubs (see the Module & library support
  /// section of doc/python-frontend-architecture.md; open work in
  /// doc/python-frontend-plans.md §6). When a call's resolved symbol
  /// is in this map, the front-end emits a call to the named C function
  /// instead of calling the Python body. The C function is resolved
  /// at link-to-library time; the declared Python signature must
  /// match the C function's argument and return types.
  std::map<irep_idt, std::string> c_intrinsic_map;

  /// Functions decorated with ``@may_raise('ExcType')`` (e.g. the
  /// os.* file-operation stubs -> 'OSError'). Maps the function's
  /// qualified id to the exception type name. Under the opt-in
  /// --python-raising-ops-check, a call to such a function emits a
  /// nondet-guarded may-raise (see emit_may_raise); otherwise it is
  /// ignored. Populated by the decorator scan alongside
  /// ``c_intrinsic_map``.
  std::map<irep_idt, std::string> may_raise_map;

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

  /// §12b: names bound by assignment somewhere in the current function
  /// (Python's "assigned anywhere => local" rule), minus global/
  /// nonlocal. A read of one of these before its local symbol exists
  /// is reported as UnboundLocalError. Saved/restored per function.
  std::set<std::string> current_function_locals;
  /// §12b: subset of current_function_locals bound ONLY by plain
  /// `x = ...` Assign (never AnnAssign/AugAssign/tuple/for/with/except/
  /// walrus/lambda-alias). Each gets a runtime `<qname>$bound` bool,
  /// false at function entry, set true after the binding statement (at
  /// the convert_statement chokepoint, keyed on the AST target so it is
  /// independent of how convert_assign built the value write), and
  /// asserted at every read. Path-sensitive: a read on a branch that
  /// didn't assign it (cross-branch UnboundLocalError) is detected.
  std::set<std::string> current_function_bit_locals;

  /// Whether to use mathematical integers instead of int64.
  bool unbounded_ints = false;
  bool processing_import = false;
  bool no_body_check = false; // suppress no-body-for-callee properties

  /// When true (opt-in via --python-raising-ops-check), operations
  /// that can raise an exception at runtime but whose preconditions
  /// the frontend cannot prove (int()/float() of a non-constant
  /// string -> ValueError, os.remove/rmdir/mkdir -> OSError, re with
  /// a non-str pattern -> TypeError, ...) are modeled as
  /// may-raise (a nondet-guarded __exception_active) so the
  /// uncaught-exception / except path is explored. Default OFF: the
  /// default models these as silently succeeding (precision-favoring,
  /// per the historical false-positive concern), so the ESBMC sweep
  /// is unaffected unless the flag is set.
  bool python_raising_ops_check = false;

  /// When true, promote front-end quiet-by-default diagnostics
  /// (Slice / Yield / YieldFrom, unresolved method / function /
  /// attribute access, subscript/for-in/'in' fallbacks) back to
  /// warning level. Intended for debugging spurious verification
  /// results where a silent over-approximation may be at fault.
  bool python_strict_warnings = false;
  bool use_smt_string_native = false;

public:
  void set_no_body_check(bool v)
  {
    no_body_check = v;
  }

  void set_python_raising_ops_check(bool v)
  {
    python_raising_ops_check = v;
  }

  void set_python_strict_warnings(bool v)
  {
    python_strict_warnings = v;
  }

  /// Select the native SMT-String representation (--python-smt-strings,
  /// Plan A): strings are smt_string_typet (the SMT-LIB String sort) rather
  /// than the refined {length,char*} struct. Requires an SMT String solver
  /// (--cvc5/--z3).
  void set_use_smt_string_native(bool v)
  {
    use_smt_string_native = v;
    python_smt_string_native_flag() = v;
  }

  void set_python_lazy_stubs(bool v)
  {
    python_lazy_stubs = v;
  }

  void set_python_no_exception_checks(bool v)
  {
    python_no_exception_checks = v;
  }

  void set_python_check_missing_methods(bool v)
  {
    python_check_missing_methods = v;
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

  void set_python_missing_return_check(bool v)
  {
    python_missing_return_check = v;
  }

  /// PLR §6.13: 'TypeError: 'NoneType' object is not iterable'.
  /// When true, `for x in iter:` / `[... for x in iter]` /
  /// `iter*( iter )` (anywhere a converter reads from an
  /// iterable) emits a property assertion that the iterable
  /// is not None at runtime. Symbolically, for python_value-
  /// typed iterables this is `iter.tag != NONE`. For literal-
  /// None iterables it's `false_exprt` (unconditional
  /// failure). Off-by-default because the symbolic check
  /// produces false positives on correctly-typed code where
  /// the iterable is a function-call result whose return tag
  /// CBMC can't statically prove non-NONE — opt-in for users
  /// who want strict TypeError detection.
  void set_python_check_iter_none(bool v)
  {
    python_check_iter_none = v;
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
  bool python_check_missing_methods = false;
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
  /// When true, every function with a non-empty annotated
  /// return type gets a missing-return property at its
  /// implicit fall-through point. The property fires only if
  /// a control-flow path reaches the implicit return without
  /// having executed an explicit return statement — which is
  /// a Python bug since the function returns None despite
  /// declaring a non-None return type. Off-by-default
  /// (opt-in via --python-missing-return-check).
  bool python_missing_return_check = false;
  /// PLR §6.13 iterating-None TypeError property. When true,
  /// every for-loop and comprehension emits a check that the
  /// iterable is not None. Off-by-default because the
  /// symbolic check produces false positives on correctly-
  /// typed code where the iterable is a function-call result
  /// (CBMC can't statically prove the returned python_value
  /// has a non-NONE tag). Opt-in via --python-check-iter-none.
  bool python_check_iter_none = false;
  /// Map from `python::<func-id>::<param-name>` → set of
  /// attribute names referenced via `param.<name>` (or
  /// `param.<name>(...)`) in the function body. Populated by
  /// `collect_param_attribute_uses` during `convert_function_def`
  /// and consulted at call sites when
  /// `python_check_any_arg_attrs` is on.
  std::map<irep_idt, std::set<std::string>> function_param_attr_uses;
  /// Per-(param-id, attr-name) set of class names that gate the
  /// attribute access via `if isinstance(param, ClassName):`.
  /// An entry whose set contains the special marker `""`
  /// indicates the attribute is also accessed UNGATED (so the
  /// caller's any-arg-attr check should never skip it).  When
  /// every recorded gate is a class name, the call-site check
  /// can skip when the argument's class is NOT in the set, since
  /// the attribute access only fires when the isinstance gate
  /// holds at runtime.
  /// PLR §3.3.5: isinstance-narrowing inside the function body.
  std::map<irep_idt, std::map<std::string, std::set<std::string>>>
    function_param_attr_gates;
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

  /// PLR §3.3.1 iterator-protocol classification of a class's __iter__
  /// (syntactic, computed at ClassDef registration): CPython requires
  /// __iter__ to return an ITERATOR -- `for x in obj` with an __iter__
  /// returning a plain list/tuple/dict/constant raises
  /// "TypeError: iter() returned non-iterator", and a broken __iter__
  /// SHADOWS the legacy __getitem__ protocol (verified against CPython).
  /// The frontend's value model erases the iterator/list distinction
  /// (iter(x) returns x), so validity is classified on the def's AST.
  enum class iter_protocol_kindt
  {
    VALID,   // provably returns an iterator: iter(...), a generator
             // function (yield), a genexp, or `return self` with __next__
    INVALID, // provably returns a non-iterator: list/tuple/dict/set/str
             // literal or numeric/None constant, or `return self`
             // without __next__
    UNKNOWN, // anything else (calls, params, mixed returns): not flagged
  };
  std::map<std::string, iter_protocol_kindt> class_iter_protocol;

  /// Classify \p cls_name's __iter__ def body (see iter_protocol_kindt).
  /// \p fdef is the FunctionDef AST node; \p has_next whether the class
  /// itself defines __next__ (for the `return self` form).
  void classify_iter_protocol(
    const std::string &cls_name,
    const jsont &fdef,
    bool has_next);

  /// MRO-aware lookup: the classification of the __iter__ that \p cls_name
  /// would actually resolve (its own, or the first classified ancestor's).
  /// UNKNOWN when nothing is classified.
  iter_protocol_kindt iter_protocol_of(const std::string &cls_name) const
  {
    auto it = class_iter_protocol.find(cls_name);
    if(it != class_iter_protocol.end())
      return it->second;
    auto mit = class_mro.find(cls_name);
    if(mit != class_mro.end())
      for(const auto &anc : mit->second)
      {
        auto ait = class_iter_protocol.find(anc);
        if(ait != class_iter_protocol.end())
          return ait->second;
      }
    return iter_protocol_kindt::UNKNOWN;
  }

  /// Per-instance class-identity provenance (see plan §1). A CLASS-tagged
  /// python_value's opaque __class_ptr points at an instance struct whose
  /// first component is __class_tag (int32), set at construction and kept by
  /// boxing (the same field isinstance() dispatches on). This reads it back:
  /// *(int32*)__class_ptr. Callers must guard with tag == CLASS.
  exprt python_value_class_tag(const exprt &value) const;

  /// tag == CLASS AND the instance's __class_tag matches one of \p owners
  /// (class names). The identity conjunct makes dunder dispatch and
  /// tag obligations SOUND: without it a CLASS-tagged instance of a class
  /// WITHOUT the dunder would silently take the owner's path (false proof --
  /// CPython raises TypeError). Owners without a registered tag id
  /// contribute nothing (conservative: identity check fails).
  exprt python_value_is_class_of(
    const exprt &value,
    const std::vector<std::string> &owners) const;

  /// Lower a python_value ITERABLE for iteration contexts (for-loops and
  /// comprehensions): emits the PLR §6.13 iterability tag obligation
  /// (catchable per §8.4) and the identity-refined single-owner __iter__
  /// dispatch into \p header (which the caller must emit BEFORE the loop),
  /// returning the list[python_value] view to iterate.
  exprt lower_pv_iterable(
    const exprt &iterable,
    code_blockt &header,
    const source_locationt &loc);

  /// The DISPATCH half of lower_pv_iterable (no obligation): the
  /// identity-refined single-owner __iter__ view of a CLASS-tagged
  /// python_value, the raw list-slot deref otherwise. Used by BINDING
  /// contexts (unwrap_value list-target): a `xs: list = <Any>` binding
  /// does not raise in CPython, so no obligation is emitted; the dispatch
  /// preserves the instance's identity/provenance across the coercion
  /// boundary (bedrock: `return response.get(k, [])` under a
  /// `-> List[...]` annotation previously collapsed to a nondet list).
  exprt pv_class_iter_view(
    const exprt &iterable,
    code_blockt &header,
    const source_locationt &loc);

  /// Map from class name to its base class names (for isinstance)
  std::map<std::string, std::vector<std::string>> class_bases;
  /// PLR §8.13: enum classes (a class deriving from enum.Enum, possibly
  /// via an alias) and their member names. An `Enum` subclass turns each
  /// `NAME = value` class attribute into a member whose `.value` is that
  /// value and whose `.name` is "NAME"; member access and `==` already
  /// work through the class-object machinery, so this only records what is
  /// needed to resolve `.value` / `.name`.
  std::map<std::string, std::set<std::string>> enum_members;
  /// Per enum class, the (common) value type of its members — what a
  /// member resolves to and therefore the effective type of a parameter
  /// or variable annotated with the enum class. Defaults to int.
  std::map<std::string, typet> enum_value_type;
  /// Variable symbol ids currently bound to an enum MEMBER (`s = E.M`), so a
  /// later `s.value` resolves to the member's stored value instead of nondet
  /// (the static `E.M.value` form already works; this covers the variable
  /// form). Reassigning the variable to a non-enum value clears it.
  std::set<irep_idt> enum_member_vars;
  /// Per class, instance field names annotated with an enum class
  /// (`self.s: SomeEnum`), so `obj.s.value` resolves to the field's stored
  /// member value (with the runtime tag) instead of a nondet attribute read --
  /// the field analogue of enum_member_vars.
  std::map<std::string, std::set<std::string>> enum_typed_fields;
  /// True if `t` is a pointer to a class-instance struct (`python_class_*`):
  /// an instance held BY REFERENCE (a concrete-class param/self, or a
  /// returned/aliased instance). Extends the list/dict by-reference machinery
  /// to instances (reference-semantics-for-instances, Phase 1: return-flow).
  bool is_instance_pointer(const typet &t) const
  {
    if(t.id() != ID_pointer)
      return false;
    const typet &b = to_pointer_type(t).base_type();
    if(b.id() == ID_struct)
      return id2string(to_struct_type(b).get_tag()).find("python_class_") !=
             std::string::npos;
    if(b.id() == ID_struct_tag)
      return id2string(to_struct_tag_type(b).get_identifier())
               .find("python_class_") != std::string::npos;
    return false;
  }
  /// Names bound to an enum base via `from enum import Enum as E` (so a
  /// class deriving from `E` is recognised as an enum). Seeded with the
  /// standard base names.
  std::set<std::string> enum_base_aliases{
    "Enum",
    "IntEnum",
    "IntFlag",
    "Flag",
    "StrEnum",
    "ReprEnum"};
  /// PLR §3.3.2.1 C3 linearization. The MRO for each class,
  /// starting with the class itself. Populated on ClassDef by
  /// compute_c3_mro().
  std::map<std::string, std::vector<std::string>> class_mro;
  /// Per-class set of attribute names that are class-level
  /// (defined at class body via AnnAssign or Assign, NOT via
  /// `self.X = ...` inside __init__). Class-level attrs need
  /// special read semantics: `obj.x` falls back to `Class.x`
  /// when the instance has not shadowed the attribute via an
  /// explicit `obj.x = v` write.
  ///
  /// To track shadow status per instance, the class struct
  /// includes a synthetic `__shadow_<attr>` boolean field for
  /// each class-level attr. Instance creation initialises it
  /// to False; instance writes (`obj.x = v`, `self.x = v`)
  /// set it to True; `del obj.x` resets it. Attribute reads
  /// then emit a ternary:
  ///   `obj.__shadow_<attr> ? obj.<attr> : Class.<attr>`.
  ///
  /// Without this distinction, instance and class storage
  /// diverge after `Class.<attr> = v` — instances that
  /// haven't shadowed still see their stale init-time copy
  /// rather than the updated class storage.
  std::map<std::string, std::set<std::string>> class_level_attrs;
  /// §11: per-class set of bare-annotation instance fields (`x: T`
  /// with no class-body value) that are NOT unconditionally assigned
  /// at the top level of __init__. Reading such a field before the
  /// instance has assigned it is an AttributeError (PLR §6.10);
  /// convert_attribute asserts the field's __shadow_ flag for these.
  std::map<std::string, std::set<std::string>> class_attrerror_fields;
  /// §11b: per-class map attr -> descriptor class name, for class
  /// attributes bound to an instance of a class defining __get__ (a
  /// custom descriptor). Attribute reads dispatch the descriptor's
  /// __get__ instead of returning the stored value.
  std::map<std::string, std::map<std::string, std::string>>
    class_descriptor_attrs;
  /// §11 inheritance support. class_all_bare[c]: every bare-annotation
  /// instance field (`x: T`, no value) declared anywhere in c's class
  /// hierarchy. class_ctor_assigned[c]: every field that constructing
  /// c() definitely assigns, accounting for the super().__init__()
  /// chain (so a subclass that omits super leaves inherited fields
  /// unassigned). attrerror = all_bare - ctor_assigned.
  /// @dataclass synthesized-__init__ support: per-dataclass ORDERED list of
  /// instance fields (in annotation order) that the synthesized __init__ binds
  /// positionally, plus the subset that carry a default (so an omitted
  /// positional arg is left at the class-level default, not nondet-bound).
  std::map<std::string, std::vector<std::string>> dataclass_init_fields;
  std::map<std::string, std::set<std::string>> dataclass_defaulted_fields;
  /// Classes whose __len__ PROVABLY returns a negative integer constant
  /// (body is a single `return <neg const>`). `len()` on such an instance
  /// raises ValueError ('__len__() should return >= 0'). Constant-only, so no
  /// false positive on a symbolic/non-negative __len__.
  std::set<std::string> class_len_negative;

  /// PLR §4.4 instance truthiness classification (syntactic, at ClassDef
  /// registration). CPython: __bool__ wins over __len__; an instance with
  /// NEITHER is always truthy. A class whose deciding dunder provably
  /// returns a constant classifies as ALWAYS_TRUTHY / ALWAYS_FALSY
  /// (`_AnyDict.__len__ -> 0` made every `if response:` guard falsy --
  /// modelling it truthy was a FALSE PROOF and a path-explosion driver);
  /// a non-constant deciding dunder classifies UNKNOWN (nondet truthiness,
  /// sound in both directions). No dunder anywhere in the MRO: TRUTHY.
  enum class truthiness_kindt
  {
    TRUTHY,
    FALSY,
    UNKNOWN,
  };
  std::map<std::string, truthiness_kindt> class_truthiness;

  /// Constant-directed dispatcher folding (perf whole-group): a PURE
  /// DISPATCHER is a function whose body is a chain of
  /// `if <param> == "lit": return ClassName()` (docstrings allowed, an
  /// optional trailing `assert False[, msg]` default), keyed by the
  /// switch param's positional index. A FORWARDER is a function whose
  /// body is a single `return g(<param>, ...)`. A call site whose switch
  /// argument is a string LITERAL folds to the selected branch's
  /// constructor (or the assert-False default), eliminating the N-way
  /// branch-join that made symex merge N service-client states per call
  /// (the boto3 client() chain: 31 branches, ~3x time / ~4x memory on
  /// mediaconvert_manager). Semantics preserved: matching literal =
  /// exactly the branch CPython takes; unmatched = the dispatcher's own
  /// assert-False contract; non-literal args do NOT fold.
  struct dispatcher_summaryt
  {
    std::size_t param_index = 0; // positional index incl. self for methods
    std::map<std::string, std::string> branches; // literal -> class name
    bool assert_false_default = false;
    std::string forwards_to; // non-empty: FORWARDER to this func id
  };
  std::map<std::string, dispatcher_summaryt> dispatcher_summaries;

  /// Detect the dispatcher/forwarder shape on \p fdef (a FunctionDef AST)
  /// and record it under \p func_id. \p first_param_index is 1 for bound
  /// methods (param 0 is self), 0 otherwise.
  void register_dispatcher_summary(
    const std::string &func_id,
    const jsont &fdef,
    std::size_t first_param_index);

  /// Fold a call to \p func_id when a dispatcher summary applies and the
  /// switch argument is a string literal. Returns the folded expression
  /// (the selected class construction, or a nondet after emitting the
  /// assert-False default) or nullopt when not foldable.
  std::optional<exprt> try_dispatcher_fold(
    const std::string &func_id,
    const jsont &args,
    const jsont &expr,
    std::size_t first_param_index);

  /// MRO-aware lookup of the instance-truthiness classification.
  truthiness_kindt truthiness_of(const std::string &cls_name) const
  {
    auto it = class_truthiness.find(cls_name);
    if(it != class_truthiness.end())
      return it->second;
    auto mit = class_mro.find(cls_name);
    if(mit != class_mro.end())
      for(const auto &anc : mit->second)
      {
        auto ait = class_truthiness.find(anc);
        if(ait != class_truthiness.end())
          return ait->second;
      }
    return truthiness_kindt::TRUTHY;
  }

  /// The CLASS arm of python_value truthiness (shared by the two truthiness
  /// builders -- python_truthiness and unwrap_value's bool path): dispatches
  /// per-instance class identity against the ClassDef-time classification.
  /// See the comment at the python_truthiness use site.
  exprt pv_class_truthiness(const exprt &e) const;
  std::map<std::string, std::set<std::string>> class_all_bare;
  std::map<std::string, std::set<std::string>>
    class_ctor_assigned; /// Per-class set of class-level attributes that were
  /// declared in THIS class's body (as opposed to inherited
  /// from a base class). PLR §3.3.2 / §9.4: class attribute
  /// reads walk the MRO at lookup time. A subclass that
  /// inherits an attribute (didn't redeclare it in its own
  /// body) should resolve `Subclass.attr` via the parent
  /// class's storage so updates to `Parent.attr` are visible
  /// through `Subclass.attr`. Without this distinction, each
  /// subclass keeps its own init-time copy and `Parent.attr =
  /// X` fails to propagate to `Subclass.attr` reads.
  ///
  /// Populated by ClassDef when the class body explicitly
  /// declares the attr via AnnAssign or Assign at class body
  /// level. Inherited attrs land in `class_level_attrs` (so
  /// the shadow-fallback machinery still applies on
  /// instances) but NOT in `class_owned_attrs` (so reads
  /// walk MRO to find the owner).
  std::map<std::string, std::set<std::string>> class_owned_attrs;
  /// PLR §6.3.1: instance attributes can be created from any
  /// function that has access to the instance, not only from
  /// methods of the class. A free function `def f(x: A): x.v
  /// = 1` introduces attribute `v` on every A instance the
  /// function operates on. Since CBMC's GOTO uses static
  /// structs, we must declare such attrs at class-definition
  /// time. This map collects them by scanning module-level
  /// (and nested) function bodies during a pre-pass; the
  /// class def picks the discovered names up alongside the
  /// method-body scan.
  std::map<std::string, std::set<std::string>> dynamic_class_attrs;
  /// PLR §3.3.2 / §9.4: per-class set of attribute names that are BOTH a
  /// method name AND assigned as an instance attribute somewhere
  /// (`c.m = v` where `m` is a method) — i.e. a method shadowed by an
  /// instance attribute (a non-data descriptor shadow). Such names get a
  /// `python_value` storage field + a `__shadow_<attr>` flag; a bare read
  /// `c.m` resolves via the runtime shadow ternary
  /// `if(__shadow_m) instance.m else <nondet>` (the unshadowed fallback is
  /// a sound nondet over-approximation — unshadowed reads of a method as a
  /// VALUE are rare and a nondet cannot produce a false proof). Method
  /// CALLS `c.m()` are unaffected (they resolve via `try_method_call`).
  std::map<std::string, std::set<std::string>> method_shadow_attrs;
  /// Per-class set of method names that are declared with
  /// @property. Attribute reads of these names call the method
  /// with self as the single argument (PLR §3.3.2).
  std::map<std::string, std::set<std::string>> class_property_methods;
  /// @property setters: class -> property name -> the setter method's symbol
  /// id (`python::<class>::<prop>__setter`). A `@<prop>.setter` accessor is
  /// stored under a DISTINCT symbol so it does not clobber the getter
  /// (`python::<class>::<prop>`); `obj.<prop> = v` dispatches the setter (a
  /// data descriptor, PLR §3.3.2) rather than a shadowing field store.
  std::map<std::string, std::map<std::string, std::string>>
    class_property_setters;
  /// All method names declared on a class (regardless of whether
  /// they have been converted yet). Populated in convert_class_def
  /// before any method body is converted, so forward-reference
  /// calls (`self.foo()` inside `__init__` where `foo` appears
  /// later in the class body) can be distinguished from genuinely
  /// missing methods.
  std::map<std::string, std::set<std::string>> class_declared_methods;
  /// PLR §3.3.1: classes whose OWN body defines `__eq__` but not `__hash__`
  /// (so `__hash__` is set to None, making instances UNHASHABLE). Consulted by
  /// is_unhashable_type at dict-key / set-element sites.
  std::set<std::string> class_eq_without_hash;
  /// PLR §3.3.2.4: `__slots__` declaration per class (the set of permitted
  /// instance attribute names). Absent key => the class did not declare
  /// __slots__ (so instances have a __dict__ and accept arbitrary attributes).
  std::map<std::string, std::set<std::string>> class_slots;

  /// PLR §3.3.2.4: true iff class \p cls is slots-enforced (it AND every
  /// user-class base in its MRO declare `__slots__`, so instances have no
  /// __dict__) AND \p attr is not one of the permitted slot names across the
  /// MRO -- i.e. assigning `obj.attr` raises AttributeError. Conservatively
  /// false when any base's slots are unknown (no false positive).
  bool
  slots_forbidden_attr(const std::string &cls, const std::string &attr) const;
  /// PLR §3.3.2.4: whether READING `cls.attr` on a slots-enforced instance is a
  /// provable AttributeError (attr not a slot / method / class-attr / dunder on
  /// a fully slots-enforced class). False-positive-free; see the definition.
  bool slots_read_forbidden(const std::string &cls, const std::string &attr);
  /// The class whose method call initiated the current super()
  /// dispatch. Set by the call site (e.g. when D() is called,
  /// set to "D"); nested super() inlining preserves it. Empty
  /// outside any dispatch.
  std::string mro_root_class;

  /// Map from variable name to function symbol (for lambda assignments)
  std::map<std::string, irep_idt> function_aliases;
  /// §12 higher-order monomorphisation: cache of specialised clones of a
  /// user higher-order function keyed by "<hof_id>$mono$<callable_ids>",
  /// so the same call pattern (e.g. a loop body) reuses one clone and
  /// distinct callables get distinct, sound clones.
  std::map<std::string, irep_idt> monomorph_cache;
  unsigned monomorph_counter = 0;
  /// §10 path-sensitive callable dispatch: per qualified name, the
  /// ordered list of distinct callable targets it has been bound to
  /// (e.g. `if c: h=f else: h=g` records [f, g]). Unlike
  /// function_aliases this accumulates across branches (it is NOT
  /// snapshot/merged), so the call site can see that a name has
  /// multiple candidates and dispatch on a runtime tag (`name
  /// $callable_tag`, set per branch at the assignment site) instead of
  /// picking the last-processed branch. Gated on size() > 1 so
  /// single-target aliasing is unchanged.
  std::map<std::string, std::vector<irep_idt>> callable_candidates;
  /// Unannotated `d = {}` symbols whose key/value type is still the
  /// dict[str,int] default. The first `d[k] = v` rebuilds the dict
  /// with the actual (homogeneous) key/value types so int/float keys
  /// aren't lossily coerced to str. Cleared once rebuilt or on full
  /// reassignment to a non-empty value.
  std::set<irep_idt> empty_dict_pending;
  /// PLR §8.7: index of the *args parameter for each function that
  /// has one. Stored explicitly because closure captures are appended
  /// after *args, so its position isn't always last.
  std::map<irep_idt, std::size_t> function_vararg_index;
  /// Call-site signature validation metadata, recorded only for
  /// locally-defined, undecorated functions (an exact signature).
  /// `function_max_positional` is the count of positional-or-keyword
  /// params (posonly + regular, including self); presence in
  /// `function_has_kwargs` means the function accepts **kwargs.
  std::set<irep_idt> function_signature_checkable;
  std::map<irep_idt, std::size_t> function_max_positional;
  /// Count of REQUIRED positional-or-keyword params (those without a
  /// default), including self. A call binding fewer of them (by
  /// position or keyword) raises TypeError "missing required positional
  /// argument"; a param bound both by position and by keyword raises
  /// "multiple values for argument". Populated alongside
  /// function_max_positional.
  std::map<irep_idt, std::size_t> function_required_positional;
  /// Names of REQUIRED keyword-only params (kwonly args with no
  /// default). A call not supplying one by keyword raises TypeError
  /// "missing required keyword-only argument". Populated alongside
  /// function_max_positional.
  std::map<irep_idt, std::set<std::string>> function_required_kwonly;
  /// Base names of POSITIONAL-ONLY params (those before the `/` marker).
  /// Passing one by keyword (when the callee has no **kwargs to absorb it)
  /// raises TypeError "got some positional-only arguments passed as keyword
  /// arguments". Populated alongside function_max_positional.
  std::map<irep_idt, std::set<std::string>> function_posonly_params;
  std::set<irep_idt> function_has_kwargs;
  // Default parameter values evaluated at definition time
  // Maps (function_name, param_index) → default value expression
  std::map<std::pair<std::string, std::size_t>, exprt> default_values;
  // PLR §8.7 mutable-default gotcha for METHODS: a method`s mutable
  // (list/dict/set) default is frozen into a static-lifetime symbol so the
  // shared state persists across calls (the module-level def path freezes its
  // own; methods, processed in convert_class_def across idempotent passes, queue
  // the once-only initialiser here and flush it into the module-init block at
  // the start of convert_module_body). `frozen_method_defaults` guards the
  // freeze so it happens exactly once per (class::method::index).
  std::vector<code_frontend_assignt> deferred_static_default_inits;
  std::set<std::string> frozen_method_defaults;
  // PLR §8.7: for a parameter whose default is a callable-valued NAME
  // (e.g. `op=cur` where `cur` was bound to a function), records the
  // callable resolved AT DEFINITION TIME. Higher-order monomorphisation
  // (try_monomorphise_call) must dispatch a defaulted call to this
  // def-time callable, NOT to the name's current binding — otherwise a
  // later reassignment of the variable would wrongly redirect the
  // default (re-resolving the live `function_aliases` entry).
  std::map<std::pair<std::string, std::size_t>, irep_idt>
    default_callable_snapshot;
  // Functions that return lambdas: maps function name to lambda id
  std::map<std::string, irep_idt> lambda_returning_functions;
  // Bound methods: maps variable name → (method_id, self_expr)
  std::map<std::string, std::pair<irep_idt, exprt>> bound_methods;
  /// §10 path-sensitive bound-method dispatch: per qualified name, the
  /// receiver (self) expression for each candidate in
  /// callable_candidates, aligned by index. Lets the call-site
  /// dispatch prepend the correct receiver per branch (`if c: m=o.a
  /// else: m=o.b`).
  std::map<std::string, std::vector<exprt>> bound_method_receivers;
  // Closure captures: maps qualified nested function name to list of
  // (outer_param_qualified_name, param_name, type) for captured variables
  std::
    map<std::string, std::vector<std::tuple<std::string, std::string, typet>>>
      closure_captures;
  /// Closure cell substrate (PLR §4.2.2), mutating slice. Per nested
  /// function qualified name, the set of NONLOCAL cell-variable names
  /// captured by a HEAP CELL pointer rather than by qualify_name's
  /// nonlocal redirect. In the nested body convert_name dereferences
  /// the corresponding pointer capture-param for these names (bypassing
  /// the redirect to the shared enclosing symbol, which would be
  /// unsound across multiple factory invocations).
  std::map<std::string, std::set<std::string>> function_cell_capture_names;
  /// Per nested function qualified name, the (cell-var, value-type)
  /// pairs whose heap cell is allocated and initialised from the
  /// enclosing scope's current value at the nested def's site. Consumed
  /// by convert_function_def to emit the allocation into the enclosing
  /// body, and the per-cell pointer symbol is python::<parent>::__cell_<v>.
  std::map<std::string, std::vector<std::pair<std::string, typet>>>
    nested_cell_allocs;
  /// Per CLOSURE-VARIABLE (qualified name of `g` in `g = factory(...)`),
  /// the binding source for each of the inner closure's captures
  /// (capture-name -> snapshot temp symbol id). This isolates distinct
  /// factory invocations: `g1 = make(); g2 = make()` get independent
  /// snapshot temps so calling g1()/g2() binds the right per-instance
  /// value (or per-instance heap cell pointer). Without this the shared
  /// closure_captures rebind would alias them (false proofs).
  std::map<std::string, std::map<std::string, irep_idt>> closure_var_captures;
  /// Closure cell substrate (PLR §4.2.2), comprehension late-binding.
  /// While converting a `[lambda ...: ... var ...]`-style comprehension
  /// whose element is a closure, convert_name resolves the loop
  /// variable `var` (bare name) to this unique per-comprehension symbol
  /// (bound to the loop's FINAL value) instead of the enclosing-scope
  /// binding. This gives Python-3 late binding (all element closures
  /// observe the final value) without clobbering an enclosing variable
  /// of the same name (which would be unsound).
  std::map<std::string, irep_idt> comprehension_var_redirect;
  /// Fat-closure registry (doc/python-frontend-fat-closure-plan.md):
  /// index -> the closure's underlying function symbol id. The index is
  /// stored in a CLOSURE python_value's __int_val; the runtime dispatch
  /// guards over registered closures of the matching arity.
  std::vector<irep_idt> closure_registry;
  /// Register `lambda_id` in the closure registry (dedup), returning its
  /// index.
  std::size_t register_closure(const irep_idt &lambda_id);
  /// The per-instance capture-record struct type for `lambda_id`, built
  /// from its closure_captures (one field per captured free variable).
  struct_typet closure_record_type(const irep_idt &lambda_id);
  /// Box a closure: allocate a heap capture record, fill it from
  /// `capture_values` (aligned with closure_captures[lambda_id]), and
  /// return a CLOSURE python_value referencing it. Allocation/fill
  /// statements are appended to `out`.
  exprt box_closure(
    const irep_idt &lambda_id,
    const exprt::operandst &capture_values,
    std::vector<codet> &out,
    const source_locationt &loc);
  /// Emit a runtime dispatch of a CLOSURE python_value `closure_val`
  /// called with `args`: a guarded choice over registered closures of
  /// matching positional arity, binding each candidate's captures from
  /// the record and calling it. Result is assigned to a fresh temp which
  /// is returned; dispatch statements are appended to pending_checks.
  /// Returns nil if no candidate matches the call arity (caller falls
  /// back to the sound nondet path).
  exprt dispatch_closure_value(
    const exprt &closure_val,
    const exprt::operandst &args,
    const source_locationt &loc);
  /// Registry indices (in `closure_registry`) that are BOUND METHODS
  /// rather than ordinary closures. A bound method `c.m` is boxed as a
  /// CLOSURE python_value whose capture record holds `self`; unlike an
  /// ordinary closure (captures appended LAST), a method's `self` is the
  /// FIRST positional parameter, so `dispatch_closure_value` prepends the
  /// capture for these indices. Lets a bound method flow as a runtime
  /// value through containers / branches / returns (PLR §3.3.2).
  std::set<std::size_t> bound_method_closures;
  /// Box a bound method `c.m` as a CLOSURE python_value capturing `self`
  /// (so it can be stored/passed/returned as a runtime value and later
  /// dispatched via `dispatch_closure_value`). Emits the capture-record
  /// allocation into `pending_checks`. Returns nil if `method_id` is not
  /// a usable code symbol.
  exprt box_bound_method(
    const irep_idt &method_id,
    const exprt &self_expr,
    const source_locationt &loc);
  /// If a bare attribute read `value.attr` names a method of `value`'s
  /// class (resolved via its MRO, excluding @property), box it as a
  /// runtime bound-method CLOSURE python_value (see box_bound_method);
  /// otherwise return nil. Used to replace the nondet over-approximation
  /// of a bare method read so the method can flow as a value (container
  /// element, branch, return) and be dispatched later.
  exprt try_box_bound_method_read(
    const exprt &value,
    const std::string &attr,
    const source_locationt &loc);
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
  /// PLR §3.2: empty-list element-type inference. For a body
  /// containing `lst = []` followed by `lst.append(X)` (or
  /// `.extend(X)`), record the inferred element type so the
  /// `[]` allocation uses the right element type instead of
  /// the int default. Without this the typecast at append
  /// time zeros struct-typed elements (e.g. strings).
  std::map<irep_idt, typet> empty_list_inferred_types;
  /// Analogous to empty_list_inferred_types but for dicts: an
  /// unannotated `d = {}` whose first `d[k] = v` is found by the
  /// pre-scan records (key_type, value_type) here, so the `{}`
  /// allocation is built with the real element types from creation
  /// (works inside loops, where a first-assign rebuild cannot). Avoids
  /// lossy coercion of e.g. int keys to the dict[str,int] default.
  std::map<irep_idt, std::pair<typet, typet>> empty_dict_inferred_types;
  /// PLR §6.3.1: symbol IDs whose value has set-semantics —
  /// created by `set(iterable)` builtin or by a Set literal
  /// (a list-typed storage but logically order-independent).
  /// Equality compare uses multiset semantics whenever either
  /// operand is in this set. The flag is propagated through
  /// Name→Name assignments so `y = set(...); y == {...}`
  /// works after one alias hop.
  std::set<irep_idt> set_semantic_symbols;
  /// PLR §3.1: type inference for unannotated parameters.
  /// Maps function name → (param index → inferred type),
  /// populated by a pre-pass that walks every Call site of
  /// the function and inspects the arg AST. Used as a
  /// fallback when the parameter has no annotation, in
  /// place of the default `python_value_type()`.
  std::map<std::string, std::map<std::size_t, typet>> inferred_param_types;
  /// Symbol identifiers whose declared annotation was
  /// `Optional[T]` (or any union including `None`). The
  /// declared type collapses to T (since None is encoded as
  /// the int sentinel), so structurally the symbol holds a
  /// non-tagged value of T. The compare path uses this set
  /// to skip the 'concrete struct vs None sentinel = false'
  /// fast-path: an Optional[T]-annotated symbol can still
  /// hold the None sentinel when assigned None, so the
  /// fast-path would lie about identity.
  std::set<irep_idt> optional_params;
  /// Annotation provenance: parameter symbol ids whose type came from a GENUINE
  /// source annotation (`def f(x: int)`), as opposed to the default
  /// python_value (Any) or a call-site-INFERRED concrete type for an
  /// unannotated parameter. Only a genuine annotation is a sound basis for a
  /// runtime tag obligation at the call boundary: an inferred/default scalar
  /// type (e.g. a lambda or unannotated param defaulted to int) really accepts
  /// Any, so asserting its tag would false-alarm. Populated at def time;
  /// consulted by coerce_call_argument.
  std::set<irep_idt> explicitly_annotated_params;
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
  /// Symbols PROVABLY bound to None -- populated at the note_mutable_
  /// extraction chokepoint (both plain- and ann-assign) and cleared on any
  /// rebind (invalidate_reassigned_symbol). A None-valued SYMBOL carries the
  /// pv-NONE struct only in its ASSIGN, not in its symbol value, so a use
  /// site sees just the symbol; this set lets orderable_category_of flag
  /// `x = None; x < 0` as a TypeError (PLR §6.10.1) instead of proving it
  /// vacuously. Conservative: cleared on ANY rebind, so a branch-merged
  /// None-or-other symbol is never flagged (no false positive).
  std::set<irep_idt> none_constants;
  void collect_escaped_mutables(const jsont &body);

  /// Extraction-then-mutate soundness (PLR reference semantics): when a MUTABLE
  /// object is extracted from a container into a variable (`r = c[i]`,
  /// `v = d[k]`), `r` is — in Python — the SAME object as the slot, so mutating
  /// `r` mutates the container. The frontend stores nested elements by value,
  /// so `r` is a copy and the mutation would NOT propagate (a false proof:
  /// `r.append(x); assert x not in c[i]`). This maps such an extracted variable
  /// to the source-container lvalue; on a subsequent mutation of the variable
  /// we conservatively havoc the source (sound over-approximation). Cleared for
  /// a variable when it is reassigned. The principled whole-group fix is
  /// reference semantics for mutable objects (heap-allocate + alias by
  /// pointer); this guard keeps the model sound until that lands.
  std::map<irep_idt, exprt> extracted_container_alias;

  /// §0 slot-alias WRITE-THROUGH (spike doc §14b): `v = d[k]` records the
  /// MATERIALISED lvalue slot (values[__dictidx_N]) alongside the havoc
  /// alias, so a later in-place mutation of v can write back through the
  /// slot (precise) instead of havocing the source. SOUNDNESS-SENSITIVE:
  /// the slot index is computed at extraction, so the entry must be
  /// DEMOTED (erased -- falling back to the sound havoc) before ANY
  /// statement that could disturb the source dict or the aliasing:
  /// any statement mentioning the source Name, and every control-flow
  /// statement (see demote_slot_aliases_for_statement). Sources in
  /// escaped_mutables are never recorded (unseen mutation).
  struct slot_aliast
  {
    exprt slot;         // index_exprt{member(values), idx_sym} -- an lvalue
                        // (nil for the KEY form below)
    irep_idt source_id; // the dict symbol
    std::string source_name; // bare name for the AST mentions-scan
    // KEY form (string-keyed fold extraction): the subscript FOLDED to the
    // tracked literal's value, so no slot expression exists; the
    // write-through emits a key-match store instead. Empty = slot form.
    std::string key;
  };
  std::map<irep_idt, slot_aliast> extracted_slot_alias;

  /// Statement-level demotion pre-scan for the write-through map (see
  /// slot_aliast): erases entries whose source name is MENTIONED anywhere
  /// in \p stmt, and clears the whole map for control-flow statements.
  void demote_slot_aliases_for_statement(const jsont &stmt);
  /// Record `lhs = <subscript of container>` when the result is (or may be) a
  /// mutable container. `rhs` is the converted RHS, `value` the RHS AST node.
  void note_mutable_extraction(
    const irep_idt &lhs_id,
    const exprt &rhs,
    const jsont &value);
  /// If `obj` (a converted method-call receiver) is an extracted mutable alias
  /// and `method_name` mutates it, havoc the source container (into
  /// pending_checks). Returns true if a havoc was emitted.
  bool invalidate_extracted_source_on_mutation(
    const exprt &obj,
    const std::string &method_name);
  bool invalidate_extracted_alias_inplace_mutation(const symbol_exprt &obj);
  void handle_alias_mutation_channels(const jsont &stmt);

  /// Havoc every extraction alias whose recorded source equals \p source,
  /// except \p except_id (the alias being mutated directly). PLR object
  /// identity: sibling extractions may be the SAME runtime object, so a
  /// mutation through one makes every sibling's by-value copy stale (a
  /// false proof if left readable).
  void havoc_sibling_extraction_aliases(
    const exprt &source,
    const irep_idt &except_id);
  /// PLR object identity: a NON-int-keyed dict value (`d["k"]`) is returned by
  /// value (not a writable lvalue like int-keyed values), so an in-place
  /// mutation through `d["k"].mutator(...)` is lost. Havoc the dict (into
  /// pending_checks) so a later read is nondet rather than the stale
  /// pre-mutation value (which would false-prove). `subscript` is the receiver
  /// `d[key]` node. Returns true if a havoc was emitted.
  /// Statement-level pre-scan (PLR §6.4/§3.1 dict-value-by-reference):
  /// for every `X[k].<mutator>(...)` in \p stmt, erase X's tracked dict
  /// literal BEFORE conversion, so the constant-key fold cannot hand the
  /// mutator (or any later read) a stale constant SNAPSHOT of a mutable
  /// container value. Reads keep full fold precision until the first
  /// mutation; after it they read the runtime lvalue slot (which sees the
  /// mutation). The receiver converts before the mutation is visible at
  /// method-conversion time, hence a PRE-scan, not a method-time hook.
  void invalidate_mutated_dict_literals(const jsont &stmt);

  bool invalidate_dict_value_on_mutation(
    const jsont &subscript,
    const std::string &method_name);
  /// PLR object identity (§9 #nested-aliasing): symbols bound to a list whose
  /// BY-VALUE mutable elements are aliased (shared) by a replicating/sharing
  /// op -- repetition `a*n`, concat `a+b`, slice `a[:]`, `a.copy()`,
  /// `list(a)`. The value-based representation does not model this aliasing, so
  /// an in-place mutation of such an element is reported as a
  /// `python-model-bound` (assert + cut) instead of silently producing a false
  /// proof (e.g. `g=[[0,0]]*3; g[0][0]=1; assert g[1][0]==0`). Read-only access
  /// and whole-slot reassignment (`g[i]=v`) stay precise. Residual (documented):
  /// function-parameter and container-stored aliases are not tracked.
  std::set<irep_idt> aliased_mutable_lists;
  /// Names bound to an ELEMENT of an aliased-mutable list (`row = g[i]` where
  /// `g` is in aliased_mutable_lists) -- a shared inner object. Mutating it in
  /// place (`row.append(..)`, `row[j]=..`) is the same unmodelled aliasing, so
  /// it is reported as a `python-model-bound` too. Propagated through a plain
  /// alias (`s = row`).
  std::set<irep_idt> shared_inner_mutables;
  /// True if `node` is `<aliased-list>[idx]` (a subscript whose base Name is in
  /// aliased_mutable_lists) OR a Name in shared_inner_mutables -- i.e. an
  /// in-place mutation of `node` is an unmodelled aliased-element mutation.
  bool is_aliased_list_element(const jsont &node);
  /// Push (to pending_checks) a python-model-bound report + path cut for an
  /// unmodelled in-place mutation of an aliased mutable element.
  void emit_aliased_mutation_guard(const source_locationt &loc);
  /// PLR §3.2: pre-scan a body for the empty-list element-type
  /// pattern. For each 'name = []' followed in the same body by
  /// 'name.append(X)' / 'name.extend(X)' where X is constant /
  /// has a known type, record the inferred element type in
  /// empty_list_inferred_types so the `[]` allocation produces
  /// a list with the right element type.
  void collect_empty_list_inferred_types(const jsont &body);
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

  /// PLR §7.12: a called function may mutate module globals (via
  /// `global X; X = ...`). Conversion-time SCALAR value tracking
  /// (string_constants / float_constants) keyed on a module-level
  /// global is therefore stale after any user-function call, so a
  /// later read must not fold against the pre-call value. Erase the
  /// global-keyed scalar entries (keys of the form `python::<name>`
  /// with no function scope); symex recovers the actual post-call
  /// value from the symbol, so this only drops a now-unsound fold,
  /// never a correct one. Locals (`python::<func>::<name>`) are kept.
  /// Container-literal maps are intentionally NOT invalidated (they are
  /// read structurally by argument unpacking converted after the call
  /// site); see the definition.
  /// When \p include_dict_literals is set (true only at the
  /// POST-argument call site, false at the pre-argument site), the
  /// global-keyed `dict_literals` tracking is also invalidated: a
  /// callee may have mutated a global dict in place (`d[k]=v`), which
  /// would otherwise leave a stale literal that folds a later
  /// `len`/membership/subscript against the pre-call contents.
  /// `list_literals`/`tuple_literals` are NOT invalidated (list
  /// membership/subscript already reads runtime, tuples are immutable,
  /// and `*c` argument unpacking reads `list_literals` structurally).
  void invalidate_global_value_tracking(bool include_dict_literals = false);

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

  /// PLR §6.2.9: build the eager append of one yielded value to the current
  /// generator's `__gen_result_<fn>` list (`data[length] = v; length += 1`).
  /// Used by BOTH the bare `yield X` statement path (convert_expr_stmt) and
  /// every expression-context yield (`x = yield X`, `f(yield X)`, ...) via
  /// convert_expression, so every yield is counted exactly once. Returns an
  /// empty block when the current function is not a generator or its result
  /// symbol is absent.
  code_blockt build_gen_result_append(const exprt &yielded_value);

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

  /// Root B (import scoping). Top-level function/class names defined
  /// in IMPORTED modules (populated by process_imported_module). A
  /// name here exists in the flat `python::` table only because a
  /// module was processed -- it is NOT a main-module definition (those
  /// are registered by convert_module_body, not here).
  std::set<std::string> imported_module_defs;
  /// Names explicitly brought into the main module's scope by an
  /// `import`/`from X import` statement (incl. asname). A name in
  /// imported_module_defs but NOT here leaked from a module that was
  /// processed without that name being imported -> a bare reference
  /// to it is a NameError (PLR §4.2). Disabled (conservative, no
  /// check) if a `from X import *` is seen, since we cannot enumerate
  /// the names it brings in.
  std::set<std::string> explicitly_imported_names;
  bool saw_import_star = false;
  /// Over-inclusive set of every name bound ANYWHERE in the MAIN module
  /// (all scopes, all binding forms), computed authoritatively by the AST
  /// server (`_all_bound_names`, from Store-context Names + def/class/arg/
  /// import/global/nonlocal/except names). The undefined-name `NameError`
  /// check (PLR §4.2.1) fires only when a referenced name is ABSENT from
  /// this set — over-inclusion is the safe direction (it can only miss a
  /// NameError, never invent one on a legitimately-bound name such as a
  /// tuple-unpack / `for` / `with`-as target).
  std::set<std::string> all_bound_names;
  /// True if the main module has `from __future__ import annotations`
  /// (PEP 563): all annotations become strings and are NOT evaluated at
  /// runtime, so they never raise NameError. Disables the
  /// undefined-annotation-name check below.
  bool future_annotations = false;

  /// PLR §4.2.1: if `ann` is a *bare-name* annotation (`x: T` / `-> T`,
  /// not a subscript / attribute / string forward-ref) whose name is bound
  /// nowhere (absent from all_bound_names) and is not a builtin, return
  /// that name — referencing it at def/assign time raises NameError.
  /// Returns "" when the annotation is fine, deferred (__future__), or not
  /// a checkable bare name.
  std::string undefined_annotation_name(const jsont &annotation) const;
  /// Top-level function/class names defined in the MAIN module
  /// (populated by convert_module_body). Such names are always in
  /// scope, so they are excluded from the leaked-name NameError check
  /// even if an imported module also defines the same name.
  std::set<std::string> main_module_defs;

  /// Bare names that are MUTATED inside some function/method body —
  /// a dict subscript-assign target (`d[k]=v`), a `global X` rebind, or
  /// a known dict-mutating method receiver (update/pop/...). Only these
  /// globals' conversion-time value tracking is invalidated at a call
  /// site (a call can only make a global stale if some function mutates
  /// it); never-mutated globals keep their folding. Populated by
  /// collect_function_global_mutations before any call is converted.
  std::set<std::string> globals_mutated_in_functions;
  /// Scan all function/method bodies in \p module_body and populate
  /// globals_mutated_in_functions.
  void collect_function_global_mutations(const jsont &module_body);
  /// Modules already handed to process_imported_module, to make
  /// transitive import resolution idempotent and break import cycles.
  std::set<std::string> processed_import_modules;
  /// Names assigned from typing.NewType('X', T) — treated as
  /// identity-call aliases so `UserId(42) == 42`.
  std::set<std::string> newtype_aliases;

  /// PLR §8.5: collections module imports. Maps the binding
  /// name (asname after 'from collections import X as Y' / bare
  /// X / fully-qualified collections.X) to the original
  /// collections symbol name ("defaultdict", "Counter", ...).
  /// Lets convert_call route the call to the right
  /// special-case constructor regardless of how the user
  /// imported the symbol.
  std::map<std::string, std::string> collections_imports;

  /// 'from math import comb / factorial / ...'. Maps the
  /// imported alias -> original math-function name. Lets
  /// convert_call route a direct call (e.g. comb(5,2)) to the
  /// math-intrinsic constant fold without going through the
  /// library placeholder.
  std::map<std::string, std::string> math_imports;

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
  /// Per-variable type annotation, keyed by qualified symbol id.
  /// Populated by convert_ann_assign with the original
  /// annotation type (before any subsequent widening). Consulted
  /// by convert_assign under --python-check-annotations to flag
  /// reassignments that violate the original annotation (e.g.
  /// `count: int = 10; count = "wrong"`).
  std::map<irep_idt, typet> variable_annotations;

  /// Per-symbol Union[X, Y, ...] component types, keyed by
  /// qualified symbol id. Populated when a parameter or
  /// variable's annotation is `Union[T1, T2, ...]` (or the
  /// equivalent `T1 | T2 | ...` PEP 604 syntax). Consulted by
  /// the call-site / assignment annotation-mismatch check
  /// under --python-check-annotations: when the parameter type
  /// is python_value (the catch-all that Union maps to) and
  /// the symbol has registered components, we additionally
  /// require the actual value's type to be category-compatible
  /// with at least one component.
  std::map<irep_idt, std::vector<typet>> union_annotation_components;
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
  /// Depth of enclosing `with` bodies whose context manager declares
  /// __exit__. Like try_depth, a `raise` here must NOT early-return:
  /// control has to reach the __exit__ call (which may suppress the
  /// exception) emitted after the with-body.
  unsigned with_cleanup_depth = 0;
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
  /// enclosing except handler. PLR §8.4: a handler naming the class, a
  /// catch-all `except Exception:` / `except BaseException:`, or a bare
  /// `except:` ALL catch the raise -- the program continues normally, so a
  /// hard ASSERT at the raise site would be a false alarm (the real-world
  /// boto3 corpus catches AttributeError/TypeError with `except Exception`
  /// as normal control flow). The earlier lint-style carve-out that ignored
  /// catch-alls contradicted PLR and produced exactly those false alarms.
  bool exception_is_caught(const std::string &exc_class) const
  {
    for(const auto &frame : active_exception_handlers)
    {
      if(
        frame.count(exc_class) > 0 || frame.count("Exception") > 0 ||
        frame.count("BaseException") > 0 || frame.count("") > 0)
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

  /// PLR §3.1: write-back assignments to be emitted AFTER the
  /// current statement. Populated by convert_user_call when a
  /// mutable-container argument is passed by reference through a
  /// promoted (element-type-converted) copy: the callee mutates
  /// the copy, so after the call returns we must copy the mutated
  /// values back into the caller's original storage for Python's
  /// reference semantics to hold. Consumed (appended after the
  /// statement) by convert_statement's flush block.
  std::vector<codet> pending_post_checks;

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

  /// Build a well-formed address-of `e` for passing by reference (e.g. a
  /// method-call `self`). A plain `address_of_exprt{e}` crashes symex
  /// ("address_arithmetic: either non-persistent array or pointer to result")
  /// when `e` is not an lvalue — notably the class-attribute SHADOW-FALLBACK
  /// ternary (`__shadow_x ? instance.x : Class.x`, an `if_exprt`), the shape
  /// behind the real-world boto3-benchmark crash whole-group. Distribute the
  /// address-of over an `if_exprt` (both arms are genuine lvalues, so
  /// reference semantics are preserved: the callee mutates the SELECTED
  /// storage); take the address directly for lvalue kinds
  /// (symbol/member/index/dereference); otherwise materialise a temp via
  /// pending_checks and return its address (by-value fallback, same tradeoff
  /// as the chained-call temp).
  exprt safe_address_of(const exprt &e, const source_locationt &loc);

  /// PLR §8.7: evaluate a function/method's default argument values for their
  /// DEF-TIME exception side effects, collecting the resulting checks — plus an
  /// uncaught-exception assertion if any default raises (the general
  /// per-statement check omits FunctionDef/ClassDef) — into `out`. Python
  /// evaluates defaults once, when the `def` executes; a raising default
  /// (`def f(a=[][0])`) raises there. Called at every def's SOURCE-ORDER
  /// position (module-level defs, methods, nested defs) so the raise propagates
  /// and a default reading an already-bound global does not spuriously fault.
  /// Uses save/clear/restore around convert_expression so the caller's
  /// pending_checks is left undisturbed.
  /// \param args_node: the def's `args` AST node.
  /// \param loc: source location for the uncaught-exception assertion.
  /// \param [out] out: block receiving the exception checks + assertion.
  void collect_def_time_default_checks(
    const jsont &args_node,
    const source_locationt &loc,
    code_blockt &out);

  /// Result of scanning a function/method body for its return type
  /// (used only when there is no explicit return annotation).
  struct inferred_returnt
  {
    /// The inferred type, or empty_typet{} when no value-returning
    /// `return` statement was found (the caller applies its own
    /// fall-through default: implicit None for free functions, the
    /// int default for methods).
    typet type = empty_typet{};
    bool has_value_return = false;
    bool has_yield = false;
    /// True when a return path genuinely yields a `python_value` VALUE
    /// (e.g. `return <union/Any-typed param>`), as opposed to the type being
    /// widened to `python_value` out of UNCERTAINTY about an unresolved
    /// forward/recursive call. Lets an annotated function's return slot widen
    /// to `python_value` for the genuine-union case (avoiding a punning false
    /// proof) without widening a forward-referencing `-> T` function.
    bool saw_python_value_return = false;
    typet yield_element_type;
  };

  /// Shared body scan that both convert_function_def (free functions)
  /// and convert_class_def (methods) use to infer an un-annotated
  /// return type. \p qualified_name keys symbol/AnnAssign lookups for
  /// `return varname` / tuple-element names; \p enclosing_class is ""
  /// for free functions and the class name for methods (enabling
  /// `return self`).
  inferred_returnt infer_return_type_from_body(
    const jsont &body,
    const code_typet::parameterst &parameters,
    const std::string &qualified_name,
    const std::string &enclosing_class);

  /// PLR §6.2.9: create the `__gen_result_<name>` eager-result list
  /// symbol for a generator and emit its initialisation into
  /// \p body_block; returns the symbol id. Shared by free functions
  /// and methods.
  irep_idt setup_generator_result(
    const std::string &qualified_name,
    const typet &list_type,
    code_blockt &body_block);

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

  /// Dispatch the method-call group (handled by
  /// python_converter_call_method.cpp): obj.method(args) on
  /// strings / lists / dicts / sets / class instances /
  /// math/random/re module receivers / generators / iterators.
  /// May rebind `func_name` to the method's name when the
  /// dispatch needs to fall back to the bare-name function
  /// resolution downstream (e.g. math.ceil() called on a
  /// non-module receiver). Returns nullopt if no method-call
  /// path produced a result (caller continues with the
  /// builtin/class-constructor/user-call dispatchers).
  std::optional<exprt>
  try_method_call(const jsont &expr, std::string &func_name, const jsont &args);

  /// String-method dispatch (handled by
  /// python_converter_call_string_methods.cpp): split, replace,
  /// format, isalpha/isdigit/etc., upper/lower, find/rfind,
  /// startswith/endswith, encode/decode, join, strip variants,
  /// count, partition, ljust/rjust/center, zfill. Returns
  /// nullopt if method_name doesn't match any of the recognised
  /// string methods.
  std::optional<exprt> try_string_method(
    const jsont &expr,
    const exprt &obj,
    const typet &obj_base_type,
    const std::string &method_name,
    const jsont &args);

  /// Dict-method dispatch (handled by
  /// python_converter_call_dict_methods.cpp): get / setdefault /
  /// pop / popitem / update / clear / keys / values / items /
  /// fromkeys / copy / __contains__. Returns nullopt if
  /// method_name doesn't match any recognised dict method.
  std::optional<exprt> try_dict_method(
    const jsont &expr,
    const exprt &obj,
    const typet &obj_base_type,
    const std::string &method_name,
    const jsont &args);

  /// List-method dispatch (handled by
  /// python_converter_call_list_methods.cpp): append, sort,
  /// reverse, pop, copy, extend, remove, index, __iter__,
  /// __contains__, count, clear, plus bytes-as-list[uint8]
  /// decode/encode. Returns nullopt if method_name doesn't
  /// match any recognised list method.
  std::optional<exprt> try_list_method(
    const jsont &expr,
    const exprt &obj,
    const typet &obj_base_type,
    const std::string &method_name,
    const jsont &args);

  /// Set-method dispatch (handled by
  /// python_converter_call_set_methods.cpp): add / remove /
  /// discard / union / intersection / difference / clear /
  /// __contains__ on the 64-bit bitmap representation.
  /// Returns nullopt if method_name doesn't match.
  std::optional<exprt> try_set_method(
    const jsont &expr,
    const exprt &obj,
    const typet &obj_base_type,
    const std::string &method_name,
    const jsont &args);

  /// Final user-function-call fallback (handled by
  /// python_converter_call_user.cpp): nested-function lookup,
  /// lambdas, function_aliases, @c_intrinsic redirection,
  /// callable-instance __call__ dispatch, and the keyword /
  /// vararg / default binding that emits the final
  /// side_effect_expr_function_callt. Always returns an
  /// expression (may be nil_exprt if everything fails).
  exprt convert_user_call(
    const jsont &expr,
    const std::string &func_name,
    const jsont &args);

  /// §12 higher-order monomorphisation. If \p hof_sym is a user
  /// function with a parameter that is *called* in its body and the
  /// matching positional argument in \p args is a resolvable callable
  /// (a lambda, or a name bound to a function/lambda), build (or reuse)
  /// a specialised clone of the function with that parameter bound to
  /// the callable, and report the clone plus which positional argument
  /// slots are the (now redundant) callable arguments. Returns true and
  /// fills \p clone_id / \p callable_positions on success; returns false
  /// (leaving the normal nondet-on-indirect-call path) otherwise.
  bool try_monomorphise_call(
    const std::string &func_name,
    const jsont &args,
    const symbolt &hof_sym,
    const irep_idt &hof_id,
    irep_idt &clone_id,
    std::set<std::size_t> &callable_positions);

  /// iter / next / len / int / float / bool / print / input /
  /// hex / oct / bin / repr / ascii / hash / chr / ord /
  /// complex / dict / set / list / reversed / enumerate /
  /// sorted / sum / range / round / divmod / str / all / any /
  /// hasattr / callable / type / isinstance / abs / min / max.
  /// Returns nullopt if func_name isn't one of the recognised
  /// builtins (so convert_call can try class instantiation /
  /// user-call fallback next). May return std::optional{nil_exprt{}}
  /// when the builtin matched but the receiver/args don't fit
  /// any of the supported patterns.
  std::optional<exprt> try_builtin_call(
    const jsont &expr,
    const std::string &func_name,
    const jsont &args);

  exprt convert_if_exp(const jsont &expr);
  exprt convert_subscript(const jsont &expr);
  exprt convert_tuple(const jsont &expr);
  exprt convert_list(const jsont &expr);
  exprt convert_attribute(const jsont &expr);
  exprt convert_attribute_impl(const jsont &expr);

  /// Native backend string-id handles (strings plan 2026-07-20): the
  /// solver-side string table `strtab : bv64 -> String` (an uninterpreted
  /// function; find_symbols declares it, the generic function-application
  /// path applies it -- ZERO backend changes). A handle h read back as a
  /// string is `strtab(h)`; allocating a handle for string s is a fresh
  /// symbol h with ASSUME strtab(h) == s.
  symbol_exprt strtab_symbol();
  exprt string_handle_to_string(const exprt &handle);
  /// PLR fold-soundness rule: a conversion-time FOLD of a builtin/method
  /// call may only fire when it models EVERY argument the call passes.
  /// Returns true when the call carries at most \p max_positional
  /// positional arguments and no keywords beyond \p allowed_keywords --
  /// otherwise the fold site must fall through to its sound nondet /
  /// symbolic fallback. Ignoring an unconsumed argument was a FALSE-PROOF
  /// class (sum(xs, start), sort(reverse=), min/max/sorted(key=),
  /// index(x, start), startswith(p, pos), set.union(a, b) all folded the
  /// argument-less semantics; ESBMC-suite adversarial tests, CPython-
  /// confirmed).
  bool fold_covers_call_shape(
    const jsont &call,
    std::size_t max_positional,
    const std::set<std::string> &allowed_keywords = {});
  exprt string_to_handle(const exprt &str);
  void emit_strtab_axiom(const exprt &h, const exprt &str);

  /// The inttab analogue for --python-unbounded-ints (see
  /// python_int_handle_type).
  symbol_exprt inttab_symbol();
  exprt int_to_handle(const exprt &val);

  /// Constant-string intern table (see string_to_handle): text -> handle id.
  std::map<std::string, long long> string_intern_ids;
  exprt convert_dict(const jsont &expr);

  /// Build a python_dict value from (key,value) pairs: element-type
  /// inference (with tagged-union promotion when heterogeneous), constant-
  /// key de-duplication (PLR §6.4, last value wins), padding, and the
  /// over-capacity guard. Shared by `convert_dict` and `dict.fromkeys`.
  exprt build_dict_value(
    std::vector<std::pair<exprt, exprt>> pairs,
    const source_locationt &loc);

  /// True if `e` is safe to substitute (constant-fold) at a program point
  /// LATER than where it was tracked — i.e. it is invariant. A value embedding
  /// a *mutable* program variable (a reassignable lvalue: a local, a boxed-leaf
  /// materialisation pointer, etc.) is NOT safe: re-evaluating it at the later
  /// point reads the variable's current value, not its value when the dict was
  /// built (the dict-literal const-fold aliasing class). Read-only constants /
  /// string-literal globals are safe.
  bool value_is_const_foldable(const exprt &e) const;
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

  /// Guard every pending check appended since index `from` by
  /// `guard`, wrapping them in `if(guard) { ... }`. Used for
  /// expression-position short-circuit / conditional evaluation
  /// (PLR §6.11 BoolOp, §6.13 IfExp): a may-raise sub-expression
  /// evaluated only on a taken branch must fire its check only
  /// under the branch's condition, not unconditionally.
  void guard_pending_checks(std::size_t from, const exprt &guard);

  /// Emit a guarded Python exception into pending_checks: when
  /// `cond` holds, set __exception_active and the __exception_type
  /// tag for `exc_type`. For data-conditional raises (e.g.
  /// ValueError when a search finds nothing).
  void emit_conditional_exception(const exprt &cond, const char *exc_type);

  /// Validate a dunder's return-type contract. If \p dunder_sym's
  /// declared/inferred return type concretely violates the contract for
  /// \p kind ("int" for __len__/__index__/__int__, "str" for __str__/__repr__),
  /// emit an unconditional TypeError into pending_checks and return true.
  /// A python_value (Any / unannotated-inferred-as-Any) return is never flagged
  /// -- a violation cannot be proven, so flagging would be a false positive.
  bool
  dunder_return_type_violation(const symbolt *dunder_sym, const char *kind);

  /// Opt-in (--python-raising-ops-check): model an operation that CAN
  /// raise `exc_type` but whose success the frontend cannot prove as
  /// may-raise (a nondet-guarded __exception_active). No-op unless the
  /// flag is set. See `python_raising_ops_check`.
  void emit_may_raise(const char *exc_type);

  /// PLR §6.10.2: validate `range()` arguments. Emits (into pending_checks)
  /// an uncaught TypeError when the positional-arg count is invalid (0, or
  /// >3) and — when `step_value` is supplied (the 3-arg form) — a
  /// ValueError conditional on `step == 0` (CPython: "range() arg 3 must
  /// not be zero"; fires unconditionally for a literal 0, and on the
  /// zero path for a symbolic step). Returns true if a fatal arity error
  /// was emitted (caller should short-circuit). Shared by the `range()`
  /// builtin handler and the `for ... in range(...)` lowering.
  bool emit_range_arg_checks(const jsont &args_json, const exprt *step_value);

  /// Validate a call against the callee's recorded exact signature
  /// (PLR §8.7): emit TypeError for too-many positional arguments or
  /// an unexpected keyword. `implicit_self` is the number of leading
  /// positional slots filled implicitly (1 for a bound method /
  /// constructor, 0 for a free function or an unbound Class.method
  /// call). No-op unless `func_key` is a checkable signature. Shared
  /// by the free-function, bound-method and constructor call sites.
  void validate_call_signature(
    const irep_idt &func_key,
    const jsont &expr,
    const jsont &args,
    std::size_t implicit_self);

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

  /// Walk a type-annotation AST and return the component types
  /// of a `Union[T1, T2, ...]` (or PEP 604 `T1 | T2 | ...`)
  /// shape. Returns an empty vector if the annotation isn't a
  /// union. Used to populate union_annotation_components for
  /// parameters / variables.
  std::vector<typet> extract_union_components(const jsont &annotation);

  /// Return true if the annotation is `Optional[T]` (or any
  /// Union/PEP-604 form that includes None). The compare path
  /// uses this to mark parameter symbols as nullable so the
  /// 'concrete struct vs None sentinel = false' fast-path
  /// doesn't lie.
  bool annotation_includes_none(const jsont &annotation);

  /// Check whether a value's type violates a recorded Union
  /// annotation for `sym_id`. Returns true if the symbol has
  /// union components and the actual type is incompatible with
  /// every component (i.e. the value isn't covered by any
  /// member of the union). Returns false when there are no
  /// components recorded, or when at least one component is
  /// compatible.
  bool union_annotation_violated(
    const irep_idt &sym_id,
    const exprt &actual_value) const;

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

  /// Structural (by-value) equality of two `python_value` operands with
  /// STATIC TAG DISPATCH -- when an operand's tag is a compile-time constant,
  /// only the matching branch is built (no eager string-solver/deref emission
  /// for dead branches). Symbolic tags fall back to a bounded full dispatch;
  /// at depth 0 a sound nondet bool is used. PLR §6.10.1.
  exprt python_value_structural_eq(const exprt &l, const exprt &r, int depth);

  /// Wrap a concrete typed value into a tagged-union value.
  exprt wrap_value(const exprt &e);
  /// Coerce a dispatch call's arguments to the callee's declared parameter
  /// types: box a non-python_value argument into a python_value parameter
  /// (wrap_value), otherwise safe_typecast. Synthetic dunder / protocol
  /// dispatch calls (__call__, __contains__, __setitem__, ...) bypass the
  /// normal convert_call argument coercion; without this they hand an
  /// unboxed value (int, tuple, ...) to a python_value parameter, which then
  /// relied on symex reinterpreting the bytes -- unsound if the callee reads
  /// the argument as a value. Mirrors the normal call path's coercion.
  void coerce_call_args(const typet &callee_type, exprt::operandst &args);


  /// Materialise `value` into a FRESH per-execution heap object (via a dynamic
  /// `ID_allocate`) and return a typed pointer to it. Unlike a static symbol,
  /// each runtime execution of the construction site gets a distinct object,
  /// so a value boxed inside a container that is built more than once (a
  /// function returning it, a loop) does not alias across instances. Backs the
  /// leaf-boxing materialisation (strings, unbounded ints).
  exprt allocate_boxed_leaf(const exprt &value, const typet &leaf_type);

  /// Int-boxing for --python-unbounded-ints: materialise a mathematical
  /// integer into a heap integer_typet symbol and return its typed address
  /// (integer*), so a full-precision int can be stored fixed-width inside a
  /// byte-imaged python_value. When unbounded ints are off, returns the value
  /// unchanged (the caller stores it inline as int64).
  exprt box_int_for_storage(const exprt &int_value);

  /// PLR §3.1/§3.3: unwrap an Any (`python_value`) *container* receiver to its
  /// concrete by-reference container lvalue, so the built-in container-method
  /// handlers (list.append/extend/insert/sort/reverse, dict.setdefault/...)
  /// dispatch on it and mutate -- and thereby propagate to -- the shared
  /// object. `method_name` selects the container only when it unambiguously
  /// belongs to a single built-in type and no user class defines it; otherwise
  /// `obj` is returned unchanged (so virtual dispatch / the existing paths are
  /// preserved). Returns `obj` unchanged when it is not a `python_value`.
  exprt unwrap_any_container_receiver(
    const exprt &obj,
    const std::string &method_name);

  /// True iff some user class defines a method named \p method_name (so an
  /// ambiguous/builtin name on an Any receiver should go to virtual dispatch
  /// rather than be treated as a built-in container method).
  bool any_user_class_defines_method(const std::string &method_name);

  /// True if class `cls` (or any ancestor on its C3 MRO) defines `method` as a
  /// code symbol. Used to detect unmodelled dynamic attribute dunders
  /// (`__getattribute__` / `__setattr__`) so attribute access on such an
  /// instance can be soundly over-approximated.
  bool class_mro_defines(const std::string &cls, const std::string &method);
  /// PLR §3.3.2 attribute-set-closure analysis (for the plain-class
  /// AttributeError-on-missing-read check). `assigned_attr_names`: every
  /// attribute NAME that appears as a Store-context `X.attr = ` target ANYWHERE
  /// in the program (a sound over-approximation of "an attribute that could
  /// exist on some instance" — keyed on the name, class-agnostic).
  /// `program_uses_dynamic_attr`: the program uses `setattr` / `.__dict__` /
  /// `vars(` (string-named injection that `assigned_attr_names` cannot see), so
  /// no attribute set can be proven closed. Populated by a module-wide pre-pass.
  std::set<std::string> assigned_attr_names;
  bool program_uses_dynamic_attr = false;
  /// PLR §7.5: raw local NAMES that appear as a `del <Name>` target anywhere in
  /// the program. Only these names get a `__del_<qname>` deleted-flag (set true
  /// on `del x`, false on any assignment to x, checked at reads to emit a
  /// conditional NameError). Keeps the deleted-state instrumentation off the hot
  /// path for every other name.
  std::set<std::string> deleted_name_targets;
  /// PLR §7.5/§6.10: attribute names that are `del`-eted somewhere in the
  /// program (`del <expr>.<attr>`). A class struct gets a `__present_<attr>`
  /// bool field for each such attr it defines, set true on any `x.a=` store,
  /// false on `del c.a`, and checked on read (`!present` -> AttributeError).
  /// Per-instance (a struct field) so aliasing is handled by by-reference
  /// instances. Kept off the hot path for classes with no del'd attrs.
  std::set<std::string> deleted_attr_targets;
  /// Get-or-create the `<qname>$deleted` bool flag symbol for a del-tracked
  /// name (local per function, static at module scope). Init is provided by the
  /// binding assignment (which sets it false); `del` sets true; reads guard.
  symbol_exprt deleted_name_flag(const irep_idt &qname);
  /// Classes that are NOT attr-set-closed regardless of the above: a class with
  /// a decorator (may inject attributes / replace the class) or a custom
  /// metaclass. Populated by the same pre-pass.
  std::set<std::string> attr_unsafe_classes;
  /// Collect `assigned_attr_names` + `program_uses_dynamic_attr` over the whole
  /// program AST (recursive; run once before conversion).
  void collect_assigned_attr_names(const jsont &node);
  /// PLR §3.3.2.4: whether class `cls` has a provably CLOSED attribute set — no
  /// `__getattr__`/`__getattribute__`, no metaclass, no class decorators, all
  /// MRO bases known — and the program uses no dynamic-attr injection. Only then
  /// is a missing-attribute read a provable AttributeError.
  bool class_attr_set_closed(const std::string &cls);
  /// PLR §3.3.2: whether READING `cls.attr` on a plain (__dict__) instance is a
  /// provable AttributeError -- the class has a closed attribute set
  /// (class_attr_set_closed), and `attr` is not a struct component / method /
  /// class-attr / dunder AND appears as NO `X.attr =` target anywhere
  /// (assigned_attr_names). False-positive-free by construction.
  bool plain_missing_attr_read(
    const std::string &cls,
    const std::string &attr,
    const struct_typet &st);
  /// PLR §6.2.9: if `arg_ast` is a Name bound to a generator with a live
  /// consumption cursor (`generator_cursors`), return that cursor symbol so an
  /// aggregating builtin (list/sum/...) can consume from `data[cursor:length]`
  /// (the REMAINING items) instead of re-yielding from 0, and mark it exhausted.
  /// Returns nullopt for a non-Name / non-generator / cursorless arg.
  std::optional<symbol_exprt> generator_cursor_for_arg(const jsont &arg_ast);
  /// PLR §6.10.1: whether `arg` is a CONSTANT list literal whose elements span
  /// 2+ distinct orderable categories (numeric / str / list / tuple / set /
  /// dict / None), i.e. a comparison-based reduction (sorted/min/max) over it
  /// raises TypeError. Constant literal only; an Any/symbolic element (category
  /// 0) never triggers it, so it is false-positive-free.
  bool constant_list_orderable_conflict(const exprt &arg);

  /// True iff \p t is a concrete user-class instance type (python_class_*)
  /// whose MRO defines no \p dunder. False for builtins / python_value (Any) /
  /// non-class types. The shared gate for the dunder-protocol-missing checks.
  bool concrete_class_lacks_dunder(const typet &t, const char *dunder);

  /// True iff \p t is a built-in mutable container (list/dict/set), which is
  /// unhashable -- using one as a dict key or set element raises TypeError.
  bool is_unhashable_type(const typet &t);

  /// PLR §3: the canonical integer key for a constant that participates in
  /// Python numeric key/element equality (`1 == 1.0 == True`, and all hash
  /// equal), used to dedup set-literal elements and dict-literal keys. Returns
  /// the integer value for an int / bool / INTEGRAL-float constant; std::nullopt
  /// otherwise (a non-integral float, a string, or a non-constant — those dedup
  /// by exact equality or not at all).
  std::optional<mp_integer> python_numeric_key(const exprt &v) const;
  /// PLR §3: unified canonical hash/equality key of a CONSTANT container
  /// key/element over the full constant lattice (numeric cross-type /
  /// non-integral float / str value / None singleton / tuple element-wise).
  /// nullopt for symbolic/unknown (never merged). See the definition.
  std::optional<std::string> canonical_key(const exprt &e) const;

  /// PLR §8.7: the number of positional slots a `*`-unpacked call argument
  /// fills, when statically known (a list/tuple literal with no nested spread);
  /// std::nullopt otherwise (so the call-arity check is conservatively skipped).
  std::optional<std::size_t> static_unpack_length(const jsont &value) const;

  /// PLR §6.7: true iff applying binary operator \p op to operands of the given
  /// (already-converted) types is a PROVABLE TypeError — the "incompatible
  /// operands" condition `convert_bin_op` uses to fire a TypeError (e.g.
  /// `int + str`, `list + int`, `str + int`). Shared with the augmented-assign
  /// path (`x += y` applies the same operator). Any/`python_value` operands are
  /// never flagged (no false positive).
  bool binop_operand_type_error(
    const std::string &op,
    const exprt &left,
    const exprt &right) const;

  /// Orderable category of an operand for the mixed-type ordering TypeError
  /// check: 1 = numeric (int/float/bool), 2 = str, 0 = unknown/not-flaggable.
  /// Recovers the category from a CONSTANT python_value's static tag too, so a
  /// boxed literal element (e.g. "a" in the mixed list [1, "a"]) is seen as
  /// str. A symbolic/Any python_value (no static tag) returns 0 -- never
  /// flagged, so the check stays false-positive-free.
  int orderable_category_of(const exprt &e);
  /// PLR §3.3.1: whether `e` is a PROVABLY non-iterable scalar (concrete
  /// numeric / complex / constant None). Shared by the for-loop, unpack and
  /// comprehension sites; PROVABLE scalars only (Any / class / str / containers
  /// never fire), so it is false-positive-free.
  bool provably_non_iterable_scalar(const exprt &e) const;
  /// Value category for format-spec validation: 1=int, 2=float, 3=str,
  /// 0=other/Any (never flagged).
  int value_format_category(const typet &t);

  /// Format-spec whole-group rule: given a conversion/presentation \p code
  /// (lowercased; 0 = none), the value's \p cat (see value_format_category) and
  /// whether this is %-style (TypeError) vs {}/f-string-style (ValueError),
  /// return the exception name if the code is INCOMPATIBLE with the value type,
  /// else nullptr. Returns nullptr for cat==0 (unknown) so it is FP-free.
  const char *format_code_violation(char code, int cat, bool percent);

  /// PLR §3.3: dispatch a container method whose name is shared across built-in
  /// containers (pop / remove / clear / copy / update) on an Any
  /// (`python_value`) receiver, by branching on the runtime `__tag`. Each
  /// candidate container's handler runs on its by-reference view and its
  /// emitted effects are guarded by `__tag == <that container>`, so exactly
  /// the live container is mutated (and its element/None result selected).
  /// Returns std::nullopt when \p obj is not a `python_value`, when the method
  /// is not an ambiguous container method, or when a user class defines it.
  std::optional<exprt> dispatch_any_container_method_by_tag(
    const jsont &expr,
    const exprt &obj,
    const std::string &method_name,
    const jsont &args);

  /// Tag-aware equality of two python_value operands (string content
  /// for STR, scalar payload otherwise). Used to match value-typed
  /// (heterogeneous) dict keys, where declared-type-gated string
  /// equality does not apply.
  exprt value_equal(const exprt &a, const exprt &b);

  /// Erase any cached list_literals entry whose stored struct references the
  /// symbol `sym`. Called when `sym` is (re)assigned: a cached list built from
  /// `sym` (`xs = xs + [b]`) becomes STALE once `sym` changes (`b = ...`), and
  /// a later fold/read reusing the cached struct would read the new value -- a
  /// false proof. Keeps symbol-bearing caches (precision) but drops them on
  /// reassignment (soundness).
  void invalidate_list_literals_referencing(const irep_idt &sym);

  /// Canonical "a Name is being (re)assigned" tracking invalidation. Call this
  /// at EVERY reassignment site (plain/ann/aug assign, tuple-unpack targets,
  /// walrus, for-target, finally-assigned names, ...) so stale tracking cannot
  /// survive the rebind. It (a) drops cached list/tuple/dict literals that
  /// REFERENCE `sym` (stale-symbol-in-container), and (b) clears `sym`'s own
  /// scalar constants (float/string). A site that then rebinds `sym` to a
  /// constant/literal re-establishes the precise tracking AFTER calling this.
  /// Consolidates a rule that was previously replicated per-site and repeatedly
  /// missed (unpack/walrus/try-split/finally stale-tracking false proofs).
  void invalidate_reassigned_symbol(const irep_idt &sym);

  /// If `e` is a side-effecting call (a side_effect_expr_function_callt with a
  /// value), materialise it into a fresh temp via `pending_checks` and return
  /// the temp; otherwise return `e` unchanged. Used by operand-DUPLICATING
  /// builtins (abs/min/max) so a mutating call argument executes exactly ONCE
  /// instead of once per duplicated branch (a false proof otherwise).
  exprt materialize_call_operand(const exprt &e);

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

  /// Length of a string/list/dict expression. For native SMT-String operands
  /// (smt_string) this emits cprover_string_length_func (→ str.len); otherwise
  /// it reads the struct's .length member.
  exprt native_or_member_string_length(const exprt &s);

  /// A fresh nondet native SMT String whose length is constrained to
  /// [0, PYTHON_MAX_STRING_LENGTH]. The bound keeps len() of a nondet string
  /// both sound and precise: str.len is an unbounded SMT Int and its int2bv to
  /// signed 64-bit can appear negative for spurious astronomically-long
  /// strings, which would otherwise make `len(s) >= 0` unprovable. Mirrors the
  /// refined backend's length bound. Emits the nondet assignment and the bound
  /// assumption into pending_checks; returns the bounded symbol.
  exprt bounded_nondet_string(const source_locationt &loc);

  // --- Representation-neutral string primitives (Plan A) ----------------
  // These are the single place each string operation branches on backend
  // (native smt_string vs refined {length,data} struct). New sites should
  // call these rather than open-coding member access / str.* intrinsics, so a
  // backend change (and the eventual hybrid retirement) is localized here.

  /// Build and register a native SMT-String producing/relational intrinsic
  /// application (e.g. str.++ / str.substr), returning it typed as `ret`.
  exprt native_string_app(
    const irep_idt &fn,
    std::vector<typet> arg_types,
    const exprt::operandst &args,
    const typet &ret);

  /// Refined-backend {length,data} struct view of a string expression.
  exprt string_struct_view(const exprt &s);

  /// Concatenation a + b (returns a string of the active representation).
  exprt string_concat(const exprt &a, const exprt &b);

  /// Native SMT-String back-end: alias a produced native string to a fresh
  /// symbol carrying an explicit length hint `len(result) == length_hint`, so
  /// exact `len()` relations over the result are decidable without the
  /// solver having to reason across the `int2bv(str.len ...)` boundary (which
  /// times out for `int2bv(a+b)` vs `bvadd(int2bv a, int2bv b)`). Sound: under
  /// the per-string `str.len < 2^63` bound the hint is implied by the alias.
  /// \p produced must be an `smt_string`; \p length_hint is its length as a
  /// python int.
  exprt bind_string_length_hint(
    const exprt &produced,
    const exprt &length_hint,
    const source_locationt &loc);

  /// Substring s[start : start+len] (returns a string).
  exprt string_substr(const exprt &s, const exprt &start, const exprt &len);

  /// Content equality a == b (returns bool).
  exprt string_equal(const exprt &a, const exprt &b);

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

  /// Coerce a single call argument to a declared parameter type.
  ///
  /// Centralised home for PLR-defined call-boundary adaptations
  /// that would otherwise produce undefined-behaviour goto code
  /// through the generic safe_typecast / unwrap_value path:
  ///   - PLR §3.2: a python_value{NONE} argument bound to a
  ///     python_string-typed parameter (`f(None)` for
  ///     `def f(s: str)`) is rewritten to the canonical
  ///     {0, NULL} length-0 marker. This is recognised by the
  ///     length-0 Optional[str] fast-path at compare sites
  ///     (cluster v9), so `s is None` correctly returns True
  ///     inside the callee. Without this rewrite, the generic
  ///     unwrap_value path emits `*(python_value{NONE}.__str_ptr)`
  ///     which is a NULL deref the symex would otherwise treat
  ///     as nondet — sound by accident and a frequent source of
  ///     verification surprises.
  ///   - Same shape applies to int (sentinel), float (sentinel-
  ///     cast double), and (TODO) list/dict.
  ///
  /// All call-boundary code paths in the frontend (user-call
  /// dispatch, class-constructor calls in convert_call /
  /// convert_assign / convert_for / convert_with, and super()
  /// dispatch in convert_call_method) should funnel single-arg
  /// coercion through this helper so the same PLR-defined
  /// adaptations are applied uniformly. Implemented as a thin
  /// wrapper over `coerce_to_typed_slot` (the same rule applies
  /// to assignment-RHS and return-value boundaries via
  /// `coerce_assign_rhs` and `coerce_return_value`).
  /// \param param_id: the parameter's symbol identifier, when known. If it
  ///   names an explicitly-annotated scalar parameter, a runtime tag
  ///   obligation is emitted (a tagged-union/Any arg whose runtime tag does
  ///   not match the annotated scalar type raises TypeError under CPython).
  exprt coerce_call_argument(
    const exprt &arg,
    const typet &param_type,
    const irep_idt &param_id = irep_idt{});

  /// Coerce a value being assigned to a variable of a declared
  /// type. Same PLR adaptations as `coerce_call_argument`
  /// (None markers per target type) — the assignment boundary
  /// is just another typed slot in PLR's terms.
  ///
  /// Use at every site that emits `code_frontend_assignt` whose
  /// LHS has a declared natural type that differs from the RHS.
  exprt coerce_assign_rhs(const exprt &rhs, const typet &lhs_type);

  /// Coerce a return-value expression to the function's
  /// declared return type. Same PLR adaptations as
  /// `coerce_call_argument`.
  ///
  /// Use at every site that emits `code_frontend_returnt`
  /// whose value has a different type than the function's
  /// declared return.
  exprt coerce_return_value(const exprt &ret_val, const typet &return_type);

  /// Coerce a container element to the container's declared
  /// element type. Same PLR adaptations as
  /// `coerce_call_argument` — the element-binding boundary in
  /// PLR's gradual type system is just another typed slot.
  ///
  /// Use at every site that constructs or appends an element
  /// to a typed list / dict / set / tuple field whose
  /// element type differs from the source value's type.
  /// Centralises the "list[None, 'abc']" pattern (mixed
  /// element types) where None binds as the element-type's
  /// canonical None marker rather than going through the
  /// generic safe_typecast / unwrap_value NULL-deref path.
  exprt coerce_element(const exprt &elem, const typet &element_type);

  /// §1 (BMC container bound): emit assert(length < cap) followed by
  /// assume(length < cap) into `block` before a container-growth store
  /// (list append/insert, dict insert, ...). The model holds at most
  /// `cap` elements; appending at index == cap would write past the
  /// modelled array and silently corrupt state. The assert reports a
  /// path that exceeds the verifier's container capacity (property
  /// class "python-model-bound") rather than mis-modelling it; the
  /// assume then cuts that path so no corrupt-state execution is
  /// explored. Sound: a beyond-capacity execution is reported or cut,
  /// never silently trusted.
  void emit_capacity_guard(
    code_blockt &block,
    const exprt &length,
    long cap,
    const source_locationt &loc = source_locationt{});

  /// Container-PRODUCER capacity guard. Push assert(count <= cap) +
  /// assume(count <= cap) into `checks` where `count` is the element COUNT
  /// (final length) of a freshly built or grown container — list repetition
  /// `l * n`, concatenation `a + b`, `extend`, slice-assignment, an
  /// over-long literal. Unlike \ref emit_capacity_guard (which guards the
  /// pre-write index `length < cap` at an append/insert), this guards the
  /// resulting count, so a container of exactly `cap` elements (filling
  /// indices 0..cap-1) is allowed. Same python-model-bound property: a
  /// beyond-capacity construction is reported, then the path is cut, never
  /// silently truncated. Pushes into the `pending_checks`-style side-effect
  /// list used by expression-context producers.
  void emit_count_capacity_guard(
    std::vector<codet> &checks,
    const exprt &count,
    long cap,
    const source_locationt &loc = source_locationt{});

  /// Integer-arithmetic model-bound guard (DEFAULT 64-bit int model only).
  /// Python ints are unbounded, but the default model represents them as
  /// signedbv[64]. When an arithmetic op would exceed that range it silently
  /// WRAPS (a false proof). Mirroring the container-capacity guards, push
  /// assert(no_overflow) [property class "python-model-bound"] +
  /// assume(no_overflow) into pending_checks: an overflowing computation is
  /// REPORTED (the verifier's 64-bit bound is exceeded) and the wrapping path
  /// is cut, instead of silently wrapping. No-op under --python-unbounded-ints
  /// (operands are integer_typet, which cannot overflow); callers guard on
  /// signedbv operands. `no_overflow` is the "operation does not overflow"
  /// predicate.
  void emit_int_overflow_guard(
    const exprt &no_overflow,
    const source_locationt &loc = source_locationt{});
  /// assume(idx < cap) into `checks`, where `idx` is a (normalized,
  /// non-negative) element index about to read/write the modelled data array
  /// of `cap` slots. This is the whole-group catch-all: it fires for an
  /// over-capacity list produced by ANY path (including augmented `+=`/`*=`,
  /// `list(iterable)`, or a future producer with no construction-time guard)
  /// and for the read side (which the producer guards do not cover), rather
  /// than silently returning unmodelled (nondet) data. The IndexError check
  /// (idx < length, Python semantics) is separate; this is the model bound
  /// (idx < capacity). Same python-model-bound property: reported, then cut.
  /// Fires only for a *valid* index (idx < length) that exceeds capacity, so a
  /// normal out-of-range IndexError is not misreported as a model bound.
  void emit_index_capacity_guard(
    std::vector<codet> &checks,
    const exprt &idx,
    const exprt &length,
    long cap,
    const source_locationt &loc = source_locationt{});

  /// Set bitmap range guard. A Python set is modelled as a 64-bit bitmap over
  /// elements [offset, offset+64) (offset is 0 in all current constructors),
  /// so an element outside that range cannot be represented and would be
  /// SILENTLY DROPPED by the `1 << (elem - offset)` shift -- an unsound false
  /// proof (e.g. `100 not in {0, 100}` would hold). Push assert + assume that
  /// `0 <= elem < 64` (python-model-bound), so an out-of-range element is
  /// reported then cut rather than lost. Used at every set element ADD site
  /// (literal with a runtime element, `set(iterable)`, `set.add`). When
  /// `included` is non-nil, the guard is gated on it (`included ==> in-range`),
  /// for producers that only add the element on a condition (e.g. a
  /// `set(list)` loop where `idx < length`).
  void emit_set_range_guard(
    std::vector<codet> &checks,
    const exprt &elem,
    const source_locationt &loc = source_locationt{},
    const exprt &included = nil_exprt{});

  /// Coerce all arguments in `args` to the parameter types
  /// declared in `params`. Out-of-range entries on either side
  /// are left untouched (callers are responsible for padding
  /// missing arguments with defaults beforehand).
  void coerce_call_arguments(
    exprt::operandst &args,
    const code_typet::parameterst &params);

private:
  /// Internal: shared body of `coerce_call_argument`,
  /// `coerce_assign_rhs`, and `coerce_return_value`.
  ///
  /// PLR §3.2 None-marker binding rules are uniform across
  /// every typed-slot boundary in Python's gradual type system,
  /// so all three public boundary helpers share this
  /// implementation. The boundary-specific public helpers exist
  /// solely to make each PLR call-site grep-able by intent.
  exprt coerce_to_typed_slot(const exprt &expr, const typet &target_type);

public:
  /// Look up the `__init__` symbol for `class_name`, walking
  /// the C3 MRO if the class doesn't define `__init__` itself.
  ///
  /// Without the MRO walk, subclasses that rely on the parent's
  /// `__init__` (a common pattern) would silently skip
  /// initialisation, leaving instance fields zero.
  ///
  /// Returns the resolved `(init_id, init_sym)` pair: `init_sym`
  /// is non-null on success and points to the symbol-table
  /// entry; `init_id` is its qualified name. Both fields are
  /// empty / nullptr if no `__init__` could be found in the MRO.
  std::pair<irep_idt, const symbolt *>
  lookup_init_via_mro(const std::string &class_name) const;

  /// Build the side-effect function-call expression that
  /// represents `ClassName(args...)` constructor invocation.
  ///
  /// Performs the full PLR §9.3 class-constructor sequence in
  /// one place:
  ///   1. Resolves `__init__` via the C3 MRO
  ///      (`lookup_init_via_mro`) — returns `std::nullopt` if no
  ///      matching constructor exists.
  ///   2. Inserts `address_of(self_lvalue)` as the first
  ///      positional argument (the implicit `self`).
  ///   3. Converts each positional argument from `call_node`'s
  ///      `args` array via `convert_expression`.
  ///   4. Matches each keyword in `call_node`'s `keywords` array
  ///      to the corresponding parameter by base-name and stores
  ///      it at the matched index, leaving gaps as `nil_exprt`.
  ///   5. Pads any unprovided positional argument with
  ///      `safe_zero(param_type)` and replaces nil-gap entries
  ///      with the same.
  ///   6. Coerces every argument through `coerce_call_argument`
  ///      so PLR §3.2 None-marker rewrites apply.
  ///
  /// The returned expression carries its own source location
  /// (`loc`) and an empty return type (constructor calls have
  /// no return value — `self_lvalue` is mutated in-place).
  /// Callers wrap it in a `code_expressiont` and push to their
  /// enclosing block.
  ///
  /// `call_node` must be a Python AST `Call` node so the helper
  /// can inspect its `args` and `keywords` members. Pass the
  /// raw JSON node from the AST; do not pre-convert.
  ///
  /// Centralises the constructor sequence that was previously
  /// open-coded across `convert_call`, `convert_assign`
  /// (Attribute target and Name target), `convert_for` /
  /// statement-level class init in convert_control, and the
  /// with-stmt context-manager init in `convert_except`.
  std::optional<side_effect_expr_function_callt> build_class_init_call(
    const std::string &class_name,
    const exprt &self_lvalue,
    const jsont &call_node,
    const source_locationt &loc);

  /// @dataclass construction: when `class_name` is a @dataclass WITHOUT an
  /// explicit/inherited __init__, the synthesized __init__ binds each
  /// constructor argument (positional, then keyword) to the corresponding
  /// annotated field in declaration order. Returns the field-binding
  /// assignments, or nullopt if not such a dataclass. Sound: mirrors CPython's
  /// generated __init__ exactly (field order + defaults).
  std::optional<code_blockt> build_dataclass_init_block(
    const std::string &class_name,
    const exprt &self_lvalue,
    const jsont &call_node,
    const source_locationt &loc);

  /// Unified construction emission: the statements that initialise a freshly
  /// allocated instance of `class_name` from the `ClassName(args)` call -- the
  /// __init__ call when one exists, otherwise the synthesized @dataclass field
  /// binding (or empty for a plain class with neither). Single chokepoint so
  /// every construction site (assignment, expression, return, with) handles
  /// both uniformly.
  std::vector<codet> build_class_construction(
    const std::string &class_name,
    const exprt &self_lvalue,
    const jsont &call_node,
    const source_locationt &loc);
  /// Returns std::nullopt if no class in the MRO owns the
  /// attr — which means the attr is purely instance-level
  /// (e.g. assigned only via `self.X = ...` inside __init__)
  /// and the caller should fall back to the original class's
  /// struct field directly (no MRO redirection needed).
  ///
  /// Use at attribute READ sites where the value being read
  /// from might be a class object: instance shadow-fallback
  /// ternary, direct `Class.attr` reads on the class object
  /// symbol. Instance-level attrs (always class-instance-
  /// only) bypass this and use the local struct field.
  std::optional<symbol_exprt> mro_owner_class_object(
    const std::string &class_name,
    const std::string &attr) const;

  /// §11b: dispatch an @property read `obj.attr`. Resolves the getter
  /// method via the MRO of `class_name` (so inherited properties work)
  /// and emits the call `getter(self_ptr)` into a fresh temporary,
  /// returning it. `self_ptr` must be a pointer to the instance.
  /// Returns nil_exprt if `attr` is not a resolvable property getter.
  exprt emit_property_get(
    const std::string &class_name,
    const std::string &attr,
    const exprt &self_ptr,
    const source_locationt &loc);

  /// §11b: CPython's __getattr__ hook. When `value.attr` is not found
  /// by normal lookup and `value`'s class (walked via the MRO) defines
  /// __getattr__, emit `__getattr__(self, "attr")` into a temporary and
  /// return it; otherwise return nil_exprt (caller over-approximates).
  exprt emit_getattr_fallback(
    const exprt &value,
    const std::string &attr,
    const source_locationt &loc);

  /// §11b: dispatch a custom descriptor read. If `attr` of `class_name`
  /// (walked via the MRO) is bound to a descriptor instance (a class
  /// defining __get__), emit `__get__(descriptor, obj, None)` into a
  /// temporary and return it; otherwise return nil_exprt. `obj_ptr` is
  /// a pointer to the instance being accessed.
  exprt emit_descriptor_get(
    const std::string &class_name,
    const std::string &attr,
    const exprt &obj_ptr,
    const source_locationt &loc);

  /// PLR §3.3.2: a DATA descriptor (a class attribute bound to an
  /// instance whose class defines `__set__`) intercepts attribute
  /// assignment. If `attr` of `class_name` (walked via the MRO) is such
  /// a descriptor, return the statement `desc.__set__(descriptor, obj,
  /// value)` (so per-instance state lives wherever `__set__` puts it,
  /// typically the instance's own fields), else std::nullopt. `obj_ptr`
  /// is a pointer to the instance being assigned.
  std::optional<codet> emit_descriptor_set(
    const std::string &class_name,
    const std::string &attr,
    const exprt &obj_ptr,
    const exprt &value,
    const source_locationt &loc);

  /// PLR §3.3.2: dispatch a @property setter on `obj.<attr> = value`. Resolves
  /// `attr` as a property setter across `class_name`'s MRO and, if found,
  /// returns a call `setter(obj_ptr, value)`. Returns nullopt when `attr` is
  /// not a property setter (the caller then performs the normal store).
  std::optional<codet> emit_property_set(
    const std::string &class_name,
    const std::string &attr,
    const exprt &obj_ptr,
    const exprt &value,
    const source_locationt &loc);

  /// §12c: lower a single-generator list comprehension over a runtime
  /// list (e.g. a `list` parameter, whose length is symbolic) into a
  /// real GOTO while-loop that populates a fresh temporary, instead of
  /// dropping the assignment. `iter_list` is the (dereferenced) source
  /// list value; `ifs` are the generator's filter conditions (ANDed).
  /// The loop body, its element-expression checks, and the capacity
  /// guard are emitted into pending_checks; returns the temporary list,
  /// or nil_exprt if the shape is unsupported (caller falls through).
  exprt emit_listcomp_loop(
    const jsont &elt,
    const std::string &var_name,
    const exprt &iter_list,
    const jsont &ifs,
    const source_locationt &loc);

  /// PLR §9.4: emit `obj.__shadow_<attr> = True` if `attr` is
  /// a class-level attribute of the class identified by
  /// `obj`'s struct tag AND `obj` is not the class object
  /// itself. Returns std::nullopt if no shadow update is
  /// needed (attr is instance-level, or struct doesn't have
  /// the shadow field, or obj IS the class object). On
  /// success, returns the assignment statement so the caller
  /// can splice it into its block alongside the value write.
  ///
  /// `obj` should be the lvalue receiver (already dereferenced
  /// if it was a pointer). Pass the underlying struct lvalue
  /// expression — this helper reads its struct tag and looks
  /// up class_level_attrs.
  std::optional<code_frontend_assignt>
  maybe_shadow_assign(const exprt &obj_lvalue, const std::string &attr);

  /// PLR §7.5/§6.10: setter for the `__present_<attr>` flag (attr present).
  /// Call at attribute-store sites alongside maybe_shadow_assign.
  std::optional<code_frontend_assignt>
  maybe_present_assign(const exprt &obj_lvalue, const std::string &attr);

  /// Safe zero: returns from_integer(0, type) for numeric types,
  /// or a nondet value for struct/other types.
  exprt safe_zero(const typet &type) const;

  /// Compute exception type hash for a given type name.
  /// Uses class_tag_ids if available, else sum of ASCII values.
  long exception_type_hash(const std::string &type_name) const;
};

#endif // CPROVER_PYTHON_PYTHON_CONVERTER_H
