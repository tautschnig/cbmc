/// Internal helper functions shared between python_converter.cpp
/// and its source-split siblings (python_converter_compare.cpp,
/// python_converter_lambda.cpp, etc.). These are inline so each
/// translation unit gets its own copy and avoids
/// multiple-definition link errors.
///
/// Kept out of python_converter.h because they are implementation
/// details, not part of the converter's public interface.

#ifndef CPROVER_PYTHON_CONVERTER_HELPERS_H
#define CPROVER_PYTHON_CONVERTER_HELPERS_H

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/ieee_float.h>
#include <util/json.h>
#include <util/mathematical_expr.h>
#include <util/mathematical_types.h>
#include <util/pointer_expr.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/std_types.h>
#include <util/symbol.h>

#include "python_types.h"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <set>
#include <string>
#include <vector>

/// Emit a two-string-argument bool-returning intrinsic call
/// (e.g., cprover_string_equal_func, cprover_string_contains_func).
/// Registers the function in the symbol table, creates a fresh
/// result symbol, assigns the function application into it via
/// pending_checks, and returns the result as a bool_typet.
/// CPython `repr`/`str` of a finite double: shortest round-tripping
/// decimal digits (std::to_chars) formatted with Python's rules -- fixed
/// notation when the decimal point position `decpt` is in (-4, 16], else
/// scientific with a sign and >=2 exponent digits; a whole-valued float
/// keeps its ".0". Matches CPython bit-for-bit across an 830-value
/// random+boundary battery (1e15 -> "1000000000000000.0", 1e16 ->
/// "1e+16", 1e-4 -> "0.0001", 1e-5 -> "1e-05"). Folding str(float) with
/// the previous 6-digit ostream path produced WRONG constants -- a
/// false-proof channel (ESBMC str_float_* / str_format_* _fail).
[[maybe_unused]] static inline std::string py_float_repr(double d)
{
  if(std::isnan(d))
    return "nan";
  if(std::isinf(d))
    return d < 0 ? "-inf" : "inf";
  const bool neg = std::signbit(d);
  const double ad = std::fabs(d);
  std::string out;
  if(ad == 0.0)
    return neg ? "-0.0" : "0.0";
  std::array<char, 64> buf;
  auto r = std::to_chars(
    buf.data(), buf.data() + buf.size(), ad, std::chars_format::scientific);
  std::string sci(buf.data(), r.ptr);
  const auto epos = sci.find('e');
  const std::string mant = sci.substr(0, epos);
  const int exp10 = std::atoi(sci.c_str() + epos + 1);
  std::string digits;
  for(char c : mant)
    if(c != '.')
      digits += c;
  const int ndig = static_cast<int>(digits.size());
  const int decpt = exp10 + 1;
  if(decpt > -4 && decpt <= 16)
  {
    if(decpt <= 0)
      out = "0." + std::string(-decpt, '0') + digits;
    else if(decpt >= ndig)
      out = digits + std::string(decpt - ndig, '0') + ".0";
    else
      out = digits.substr(0, decpt) + "." + digits.substr(decpt);
  }
  else
  {
    std::string m = digits.substr(0, 1);
    if(ndig > 1)
      m += "." + digits.substr(1);
    const int e = decpt - 1;
    const char es = e < 0 ? '-' : '+';
    int ea = e < 0 ? -e : e;
    char ebuf[16];
    std::snprintf(ebuf, sizeof(ebuf), "%02d", ea);
    out = m + "e" + es + ebuf;
  }
  return neg ? "-" + out : out;
}

[[maybe_unused]] static inline exprt emit_string_bool_function(
  const irep_idt &func_id,
  const exprt &str1,
  const exprt &str2,
  symbol_table_baset &symbol_table,
  std::vector<codet> &pending_checks)
{
  const typet c_bool = c_bool_typet(8);
  // Direction B: for native SMT String operands the boolean queries (equal,
  // contains, is_prefix, is_suffix) lower to a native SMT Bool via the shared
  // encoder; declare the intrinsic with a native bool result and assign it into
  // the c_bool result symbol through a typecast, so convert_typecast supplies
  // the (ite <bool> bv1 bv0). Refined struct operands keep the c_bool
  // convention (handled by the struct-operand structural fallback in
  // smt2_conv) -- an operand-type decision, not a front-end one.
  const bool native_bool_query = str1.type().id() == ID_string &&
                                 str2.type().id() == ID_string &&
                                 (func_id == ID_cprover_string_equal_func ||
                                  func_id == ID_cprover_string_contains_func ||
                                  func_id == ID_cprover_string_is_prefix_func ||
                                  func_id == ID_cprover_string_is_suffix_func);
  const typet app_result_type =
    native_bool_query ? typet{bool_typet{}} : typet{c_bool};
  irep_idt sym_id{func_id};
  if(symbol_table.lookup(sym_id) == nullptr)
  {
    std::vector<typet> arg_types;
    arg_types.push_back(str1.type());
    arg_types.push_back(str2.type());
    symbolt fs{
      sym_id,
      mathematical_function_typet(std::move(arg_types), app_result_type),
      "python"};
    fs.base_name = id2string(func_id);
    symbol_table.add(fs);
  }

  function_application_exprt app(
    symbol_table.lookup_ref(sym_id).symbol_expr(), {str1, str2});
  app.type() = app_result_type;

  // Use pending_checks.size() and symbol_table.symbols.size() as
  // monotonic uniquifiers — different translation units would
  // otherwise each have their own counter and risk name
  // collisions in the shared symbol table.
  std::string rc_name = "__str_eq_" + std::to_string(pending_checks.size()) +
                        "_" + std::to_string(symbol_table.symbols.size());
  irep_idt rc_id{"python::" + rc_name};
  if(symbol_table.lookup(rc_id) == nullptr)
  {
    symbolt rs{rc_id, c_bool, "python"};
    rs.base_name = rc_name;
    rs.is_lvalue = true;
    rs.is_state_var = true;
    symbol_table.add(rs);
  }
  exprt assigned = native_bool_query
                     ? exprt{typecast_exprt{std::move(app), c_bool}}
                     : exprt{std::move(app)};
  pending_checks.push_back(code_frontend_assignt{
    symbol_table.lookup_ref(rc_id).symbol_expr(), std::move(assigned)});

  return typecast_exprt(
    symbol_table.lookup_ref(rc_id).symbol_expr(), bool_typet());
}

/// Collect all Name references from a JSON AST node.
/// Used for closure-capture analysis (lambda, comprehensions).
[[maybe_unused]] static inline void
collect_name_refs(const jsont &node, std::set<std::string> &names)
{
  if(node.is_object())
  {
    const jsont &type_node = node["_type"];
    const jsont &id_node = node["id"];
    if(
      type_node.is_string() && type_node.value == "Name" &&
      id_node.is_string() && !id_node.value.empty())
    {
      names.insert(id_node.value);
    }
    static const char *fields[] = {
      "body",        "orelse", "handlers", "finalbody",  "test",
      "value",       "values", "targets",  "target",     "iter",
      "args",        "elts",   "keys",     "left",       "right",
      "func",        "slice",  "elt",      "generators", "ifs",
      "comparators", "ops",    "exc",      "returns",    "decorator_list",
      nullptr};
    for(const char **f = fields; *f; ++f)
    {
      const jsont &child = node[*f];
      if(!child.is_null())
        collect_name_refs(child, names);
    }
  }
  else if(node.is_array())
  {
    for(const auto &elem : to_json_array(node))
      collect_name_refs(elem, names);
  }
}

/// Closure cell substrate (PLR §4.2.2): collect the Name references
/// that occur *inside* nested FunctionDef / AsyncFunctionDef / Lambda
/// scopes of `node`. These are the candidate free variables of nested
/// closures; intersected by the caller with the enclosing function's
/// locals+params they yield the *cell variables* — the ones that must
/// be boxed in a heap cell so an escaping closure can share/observe
/// them after the enclosing frame returns.
///
/// We harvest every ref within a nested scope (including its own
/// params/locals). Over-approximation here is sound: a non-captured
/// var wrongly classified as a cell still holds its value correctly
/// through the cell; the only cost is an unnecessary box. We do strip
/// the nested scope's *own* parameter names at the first hop to avoid
/// the common shadowing false positive.
[[maybe_unused]] static inline std::set<std::string>
collect_param_names(const jsont &func_def);

[[maybe_unused]] static inline void
collect_nested_closure_refs(const jsont &node, std::set<std::string> &names)
{
  if(node.is_array())
  {
    for(const auto &elem : to_json_array(node))
      collect_nested_closure_refs(elem, names);
    return;
  }
  if(!node.is_object())
    return;
  const jsont &type_node = node["_type"];
  const std::string t = type_node.is_string() ? type_node.value : "";
  if(t == "FunctionDef" || t == "AsyncFunctionDef" || t == "Lambda")
  {
    std::set<std::string> refs;
    collect_name_refs(node, refs);
    // Strip this scope's own parameters (they shadow the enclosing
    // binding, so a ref to them is not a capture of our variable).
    std::set<std::string> own_params = collect_param_names(node);
    for(const auto &r : refs)
      if(own_params.count(r) == 0)
        names.insert(r);
    return;
  }
  static const char *fields[] = {
    "body",
    "orelse",
    "handlers",
    "finalbody",
    "test",
    "value",
    "iter",
    nullptr};
  for(const char **f = fields; *f; ++f)
  {
    const jsont &child = node[*f];
    if(!child.is_null())
      collect_nested_closure_refs(child, names);
  }
}

/// §12b: collect names bound by an assignment anywhere in a function
/// body (Python's "assigned anywhere => local for the whole function"
/// rule). A read before the first binding is an UnboundLocalError.
/// Does not descend into nested function/class/lambda scopes. Names
/// declared `global`/`nonlocal` go in `excluded`. Only simple single-
/// Name targets of Assign / AnnAssign-with-value / AugAssign go in
/// `assigned` (tuple/for/with/except/walrus targets are deliberately
/// omitted -- their symbol-creation timing differs and flagging reads
/// of them risks false positives; a missed UnboundLocalError there is
/// sound). `non_plain` is the subset of `assigned` bound by something
/// PLR §4.2.1 / builtins: names that always resolve (a referenced name
/// equal to one of these is never a NameError). Covers builtin types,
/// builtin functions, builtin constants, the common exception names, and
/// the dunder module attributes. Shared by the undefined-name NameError
/// check (`get_var`) and the undefined-annotation-name check.
[[maybe_unused]] static inline bool
is_python_builtin_name(const std::string &id)
{
  static const std::set<std::string> names = {
    // builtin types
    "int",
    "float",
    "complex",
    "bool",
    "str",
    "bytes",
    "bytearray",
    "list",
    "tuple",
    "dict",
    "set",
    "frozenset",
    "object",
    "type",
    "range",
    "slice",
    "memoryview",
    "property",
    // common builtin exceptions
    "Exception",
    "BaseException",
    "ValueError",
    "TypeError",
    "KeyError",
    "IndexError",
    "StopIteration",
    "AttributeError",
    "ArithmeticError",
    "ZeroDivisionError",
    "NotImplementedError",
    "RuntimeError",
    "OSError",
    "FileNotFoundError",
    "GeneratorExit",
    "LookupError",
    "ImportError",
    "NameError",
    "UnicodeError",
    "MemoryError",
    "IOError",
    "EOFError",
    // builtin functions
    "abs",
    "aiter",
    "all",
    "anext",
    "any",
    "ascii",
    "bin",
    "breakpoint",
    "callable",
    "chr",
    "classmethod",
    "compile",
    "delattr",
    "dir",
    "divmod",
    "enumerate",
    "eval",
    "exec",
    "filter",
    "format",
    "getattr",
    "globals",
    "hasattr",
    "hash",
    "help",
    "hex",
    "id",
    "input",
    "isinstance",
    "issubclass",
    "iter",
    "len",
    "locals",
    "map",
    "max",
    "min",
    "next",
    "oct",
    "open",
    "ord",
    "pow",
    "print",
    "repr",
    "reversed",
    "round",
    "setattr",
    "sorted",
    "staticmethod",
    "sum",
    "super",
    "vars",
    "zip",
    "__import__",
    // builtin constants / common dunder module attributes
    "None",
    "True",
    "False",
    "NotImplemented",
    "Ellipsis",
    "__debug__",
    "__name__",
    "__file__",
    "__doc__",
    "__builtins__",
    "__spec__",
    "__loader__",
    "__package__",
    "__dict__",
    "__class__"};
  return names.count(id) > 0;
}

/// other than a plain non-alias `x = ...` Assign (AnnAssign, AugAssign,
/// or a Lambda/Name-aliased Assign); the caller bit-tracks only the
/// plain-Assign-only names and uses the conversion-time existence check
/// for the rest.
[[maybe_unused]] static inline void collect_assigned_locals(
  const jsont &node,
  std::set<std::string> &assigned,
  std::set<std::string> &excluded,
  std::set<std::string> &non_plain)
{
  if(node.is_array())
  {
    for(const auto &elem : to_json_array(node))
      collect_assigned_locals(elem, assigned, excluded, non_plain);
    return;
  }
  if(!node.is_object())
    return;
  const jsont &type_node = node["_type"];
  const std::string t = type_node.is_string() ? type_node.value : "";
  if(
    t == "FunctionDef" || t == "AsyncFunctionDef" || t == "ClassDef" ||
    t == "Lambda")
    return; // separate scope

  if(t == "Assign")
  {
    // A Lambda or bare-Name RHS makes the target a function/value
    // alias handled by an early-return path in convert_assign (no value
    // write at the chokepoint), so such targets are not bit-eligible.
    const jsont &val = node["value"];
    const std::string vt =
      val.is_object() && val["_type"].is_string() ? val["_type"].value : "";
    bool alias_rhs = (vt == "Lambda" || vt == "Name");
    const jsont &tgts = node["targets"];
    if(tgts.is_array())
      for(const auto &tg : to_json_array(tgts))
        if(
          tg.is_object() && tg["_type"].is_string() &&
          tg["_type"].value == "Name" && tg["id"].is_string())
        {
          assigned.insert(tg["id"].value);
          if(alias_rhs)
            non_plain.insert(tg["id"].value);
        }
  }
  else if(t == "AnnAssign" || t == "AugAssign")
  {
    const jsont &tg = node["target"];
    if(
      !(t == "AnnAssign" && node["value"].is_null()) && tg.is_object() &&
      tg["_type"].is_string() && tg["_type"].value == "Name" &&
      tg["id"].is_string())
    {
      assigned.insert(tg["id"].value);
      non_plain.insert(tg["id"].value);
    }
  }
  else if(t == "Global" || t == "Nonlocal")
  {
    const jsont &names = node["names"];
    if(names.is_array())
      for(const auto &n : to_json_array(names))
        if(n.is_string())
          excluded.insert(n.value);
  }

  static const char *fields[] = {
    "body",
    "orelse",
    "handlers",
    "finalbody",
    "test",
    "value",
    "iter",
    nullptr};
  for(const char **f = fields; *f; ++f)
  {
    const jsont &child = node[*f];
    if(!child.is_null())
      collect_assigned_locals(child, assigned, excluded, non_plain);
  }
}

/// Collect parameter names from a FunctionDef's args.
[[maybe_unused]] static inline std::set<std::string>
collect_param_names(const jsont &func_def)
{
  std::set<std::string> params;
  const jsont &args_node = func_def["args"];
  auto collect_from = [&](const jsont &list)
  {
    if(list.is_array())
    {
      for(const auto &p : to_json_array(list))
      {
        const jsont &arg_node = p["arg"];
        if(arg_node.is_string())
          params.insert(arg_node.value);
      }
    }
  };
  collect_from(args_node["posonlyargs"]);
  collect_from(args_node["args"]);
  collect_from(args_node["kwonlyargs"]);
  auto collect_single = [&](const jsont &arg)
  {
    if(!arg.is_null())
    {
      const jsont &arg_node = arg["arg"];
      if(arg_node.is_string())
        params.insert(arg_node.value);
    }
  };
  collect_single(args_node["vararg"]);
  collect_single(args_node["kwarg"]);
  return params;
}

/// Recursively walk a Python AST node and collect every attribute
/// access of the form `Attribute(value=Name(id=p), attr=X)` where
/// `p` is in `param_names`. For each match, add `X` to
/// `out[<canonical-param-name>]`.
///
/// One-hop aliasing: in a first pass we collect simple
/// `Assign(targets=[Name(id=alias)], value=Name(id=p))` and
/// `AnnAssign(target=Name(alias), value=Name(p))` patterns where
/// `p` is one of our parameters. Aliases are then folded into the
/// "name set" for the second pass, with attribute uses recorded
/// against the canonical parameter name. This catches the
/// `tmp = arg; tmp.method()` pattern that direct attribute-use
/// scanning misses.
///
/// Nested FunctionDef / AsyncFunctionDef / Lambda bodies are
/// skipped — they bind parameter names fresh, so `param.X`
/// inside a nested function does not refer to our parameter.
[[maybe_unused]] static inline void collect_param_attribute_uses(
  const jsont &node,
  const std::set<std::string> &param_names,
  std::map<std::string, std::set<std::string>> &out,
  std::map<std::string, std::map<std::string, std::set<std::string>>> *gates =
    nullptr)
{
  if(node.is_null())
    return;

  // First pass: collect one-hop aliases.
  // alias_to_param[alias_name] = canonical parameter name.
  std::map<std::string, std::string> alias_to_param;
  std::function<void(const jsont &)> alias_pass = [&](const jsont &n)
  {
    if(n.is_null())
      return;
    if(n.is_object())
    {
      const std::string &type = n["_type"].value;
      if(
        type == "FunctionDef" || type == "AsyncFunctionDef" || type == "Lambda")
        return;
      // Recognise direct alias assignments.
      if(type == "Assign")
      {
        const jsont &tgts = n["targets"];
        const jsont &val = n["value"];
        if(
          tgts.is_array() && val.is_object() && val["_type"].value == "Name" &&
          param_names.count(val["id"].value) > 0)
        {
          for(const auto &t : to_json_array(tgts))
          {
            if(t.is_object() && t["_type"].value == "Name")
              alias_to_param[t["id"].value] = val["id"].value;
          }
        }
      }
      else if(type == "AnnAssign")
      {
        const jsont &target = n["target"];
        const jsont &val = n["value"];
        if(
          target.is_object() && target["_type"].value == "Name" &&
          val.is_object() && val["_type"].value == "Name" &&
          param_names.count(val["id"].value) > 0)
          alias_to_param[target["id"].value] = val["id"].value;
      }
      for(const auto &kv : to_json_object(n))
        alias_pass(kv.second);
      return;
    }
    if(n.is_array())
    {
      for(const auto &item : to_json_array(n))
        alias_pass(item);
    }
  };
  alias_pass(node);

  // Second pass: walk and record attribute uses for both the
  // original parameters and any aliases we discovered.
  // PLR §3.3.5: track active isinstance gates so the caller
  // can skip attribute-error checks when the arg's class
  // doesn't match the gate (the attribute access wouldn't
  // fire at runtime).
  // active_gates[param_name] = stack of gate classes currently
  // surrounding the access. We record the SHALLOWEST gate (the
  // outermost), since deeper gates only further restrict.
  std::map<std::string, std::vector<std::string>> active_gates;

  // Helper: try to decode an If-test of the form
  //   isinstance(<paramOrAlias>, <ClassName>)
  // into a (canonical_param, gate_class) pair. Returns nullopt
  // on shape mismatch.
  auto decode_isinstance_gate =
    [&](const jsont &test) -> std::optional<std::pair<std::string, std::string>>
  {
    if(!test.is_object())
      return std::nullopt;
    if(test["_type"].value != "Call")
      return std::nullopt;
    const jsont &fn = test["func"];
    if(
      !fn.is_object() || fn["_type"].value != "Name" ||
      fn["id"].value != "isinstance")
      return std::nullopt;
    const jsont &args = test["args"];
    if(!args.is_array() || to_json_array(args).size() < 2)
      return std::nullopt;
    auto it = to_json_array(args).begin();
    const jsont &arg0 = *it;
    ++it;
    const jsont &arg1 = *it;
    if(!arg0.is_object() || arg0["_type"].value != "Name")
      return std::nullopt;
    if(!arg1.is_object() || arg1["_type"].value != "Name")
      return std::nullopt;
    std::string name = arg0["id"].value;
    std::string canonical;
    if(param_names.count(name) > 0)
      canonical = name;
    else
    {
      auto ai = alias_to_param.find(name);
      if(ai != alias_to_param.end())
        canonical = ai->second;
    }
    if(canonical.empty())
      return std::nullopt;
    std::string gate_class = arg1["id"].value;
    if(gate_class.empty())
      return std::nullopt;
    return std::make_pair(canonical, gate_class);
  };

  // PLR §3.3.2: attributes CREATED via a plain `param.attr = ...` store in this
  // function. A later read of such an attr is not a missing-attribute access
  // (the store created it), so these are subtracted from `out` after the walk.
  std::map<std::string, std::set<std::string>> created;
  std::function<void(const jsont &)> attr_pass = [&](const jsont &n)
  {
    if(n.is_null())
      return;
    if(n.is_object())
    {
      const std::string &type = n["_type"].value;
      if(
        type == "FunctionDef" || type == "AsyncFunctionDef" || type == "Lambda")
        return;

      // Recognise an isinstance gate on an If statement and
      // narrow the active_gates while walking the body. The
      // orelse branch sees the COMPLEMENT of the gate (no
      // narrowing recorded — accesses there are ungated).
      if(type == "If")
      {
        const jsont &test = n["test"];
        auto decoded = decode_isinstance_gate(test);
        if(decoded.has_value() && gates != nullptr)
        {
          active_gates[decoded->first].push_back(decoded->second);
          attr_pass(n["body"]);
          active_gates[decoded->first].pop_back();
          attr_pass(n["orelse"]);
          // Don't fall through to the generic walk.
          return;
        }
      }

      // PLR §3.3.2: a plain attribute-assignment target `o.attr = ...` CREATES
      // the attribute, so it is NOT a missing-attribute access — do not record
      // it. Still record uses in the assigned VALUE and any nested reads in the
      // targets (`o.x[i] = ...` READS `o.x`). AugAssign (`o.x += 1`) reads and
      // is a different node type, so it falls through to the generic walk and is
      // still recorded.
      if(type == "Assign")
      {
        attr_pass(n["value"]);
        if(n["targets"].is_array())
          for(const auto &t : to_json_array(n["targets"]))
          {
            if(
              t.is_object() && t["_type"].value == "Attribute" &&
              t["value"].is_object() && t["value"]["_type"].value == "Name")
            {
              // `name.attr = ...` -> attribute creation. Record it as created
              // (resolving param/alias) so a later read of it is not flagged.
              const std::string &nm = t["value"]["id"].value;
              std::string canon;
              if(param_names.count(nm) > 0)
                canon = nm;
              else
              {
                auto ai = alias_to_param.find(nm);
                if(ai != alias_to_param.end())
                  canon = ai->second;
              }
              if(!canon.empty() && !t["attr"].value.empty())
                created[canon].insert(t["attr"].value);
              continue;
            }
            attr_pass(t); // Subscript / nested target -> recurse for reads
          }
        return;
      }

      if(type == "Attribute")
      {
        const jsont &value = n["value"];
        if(value.is_object() && value["_type"].value == "Name")
        {
          const std::string &name = value["id"].value;
          // Resolve through alias chain (currently only one hop;
          // alias_to_param values are guaranteed to be in
          // `param_names`).
          std::string canonical;
          if(param_names.count(name) > 0)
            canonical = name;
          else
          {
            auto it = alias_to_param.find(name);
            if(it != alias_to_param.end())
              canonical = it->second;
          }
          if(!canonical.empty())
          {
            const std::string &attr = n["attr"].value;
            if(!attr.empty())
            {
              out[canonical].insert(attr);
              if(gates != nullptr)
              {
                auto gi = active_gates.find(canonical);
                if(gi != active_gates.end() && !gi->second.empty())
                {
                  // Record the OUTERMOST gate class — that's
                  // the most permissive narrowing in scope.
                  (*gates)[canonical][attr].insert(gi->second.front());
                }
                else
                {
                  // Ungated access — record the empty marker so
                  // the call-site check can never skip this attr.
                  (*gates)[canonical][attr].insert(std::string{});
                }
              }
            }
          }
        }
      }

      for(const auto &kv : to_json_object(n))
        attr_pass(kv.second);
      return;
    }
    if(n.is_array())
    {
      for(const auto &item : to_json_array(n))
        attr_pass(item);
    }
  };
  attr_pass(node);

  // PLR §3.3.2: an attribute CREATED by a plain `param.attr = ...` store in the
  // function is not a missing-attribute access — remove it from the recorded
  // read-uses (and its gate entries) so a create-then-read does not false-alarm.
  for(const auto &kv : created)
  {
    auto oi = out.find(kv.first);
    if(oi == out.end())
      continue;
    for(const std::string &a : kv.second)
    {
      oi->second.erase(a);
      if(gates != nullptr)
      {
        auto gp = gates->find(kv.first);
        if(gp != gates->end())
          gp->second.erase(a);
      }
    }
  }
}

/// Convert a double to a 64-bit floatbv constant expression.
/// Used for math intrinsics with floating-point constant folding.
[[maybe_unused]] static inline constant_exprt double_to_floatbv(double d)
{
  uint64_t bits;
  static_assert(sizeof(double) == sizeof(uint64_t), "double must be 64 bits");
  std::memcpy(&bits, &d, sizeof(bits));
  return constant_exprt{
    integer2bvrep(mp_integer{bits}, 64),
    ieee_float_spect::double_precision().to_type()};
}

/// Create a nondet refined string expression (length + content pointer).
/// Used as the result of string operations that the solver will constrain.
///
/// When emitted inside a function/method (\p scope non-empty), the
/// length/content output symbols are named under that function and a
/// DECL for each is pushed to \p decls, so goto-symex grants every
/// dynamic invocation a fresh instance. Without this the symbols are
/// global and a string-returning method called more than once (e.g. a
/// base method invoked directly and again via super()) piles the
/// refinement backend's conflicting content associations onto a single
/// symbol — turning UNSAT and proving downstream assertions vacuously
/// (class-attributes_fail). Module-level code runs once, so there the
/// symbols stay global and no DECL is needed.
[[maybe_unused]] static inline exprt make_nondet_string(
  symbol_table_baset &symbol_table,
  const std::string &scope = std::string{},
  std::vector<codet> *decls = nullptr)
{
  // Uniquifier based on symbol-table size — monotonic across all
  // call sites in one converter instance, TU-safe (no per-TU
  // counter).
  std::size_t ctr = symbol_table.symbols.size();
  std::string len_name = "__string_len_" + std::to_string(ctr);
  std::string ptr_name = "__string_ptr_" + std::to_string(ctr);

  const bool local = !scope.empty();
  const std::string prefix = local ? ("python::" + scope + "::") : "python::";

  irep_idt len_id{prefix + len_name};
  if(symbol_table.lookup(len_id) == nullptr)
  {
    symbolt ls{len_id, signedbv_typet{64}, "python"};
    ls.base_name = len_name;
    ls.is_lvalue = true;
    ls.is_state_var = !local;
    symbol_table.add(ls);
  }

  irep_idt ptr_id{prefix + ptr_name};
  if(symbol_table.lookup(ptr_id) == nullptr)
  {
    symbolt ps{ptr_id, pointer_typet(unsignedbv_typet{8}, 64), "python"};
    ps.base_name = ptr_name;
    ps.is_lvalue = true;
    ps.is_state_var = !local;
    symbol_table.add(ps);
  }

  symbol_exprt len_expr = symbol_table.lookup_ref(len_id).symbol_expr();
  symbol_exprt ptr_expr = symbol_table.lookup_ref(ptr_id).symbol_expr();
  if(local && decls != nullptr)
  {
    decls->push_back(code_frontend_declt{len_expr});
    decls->push_back(code_frontend_declt{ptr_expr});
  }
  return struct_exprt({len_expr, ptr_expr}, python_string_type());
}

/// Emit a cprover_string_* function application (multi-arg, string-returning).
/// Creates: return_code = func_id(result.length, result.content, args...)
/// Returns the result string expression.
[[maybe_unused]] static inline exprt emit_string_function(
  const irep_idt &func_id,
  const exprt::operandst &extra_args,
  symbol_table_baset &symbol_table,
  std::vector<codet> &pending_checks,
  bool in_loop,
  const std::string &scope)
{
  // Scope the result's output symbols to the enclosing function (when
  // known) and DECL them up-front, so each dynamic invocation gets a
  // fresh instance — see make_nondet_string. The DECLs must precede the
  // call assignment below in the emitted code.
  std::vector<codet> decls;
  exprt result = make_nondet_string(symbol_table, scope, &decls);
  for(auto &d : decls)
    pending_checks.push_back(std::move(d));

  // PLR §6.5.6 (str.__add__) inside loops: the same output
  // symbols (__string_len_N, __string_ptr_N) are emitted by the
  // frontend once per AST node and reused across loop
  // iterations. If we don't advance their SSA version
  // explicitly between iterations, the string refinement
  // backend emits conflicting constraints over a single SSA
  // value (one constraint per iteration), resulting in
  // UNSAT and any assertion verifying SUCCESSFUL
  // (github_3127_1_fail). Havoc the output args before each
  // call so SSA gives them fresh L2 indices.
  //
  // We only do the havoc when the call site is inside a loop
  // (in_loop=true). Outside loops, the call site never repeats
  // and the unchanged symbols carry no stale constraints. The
  // pointer havoc creates one fresh address-of object per call
  // per execution, which would cumulate above CBMC's 256-object
  // cap on string-heavy class re-instantiation tests
  // (github_2992_lower) if applied unconditionally. NOTE: inside a FUNCTION
  // (non-empty scope) no havoc is needed either -- make_nondet_string makes
  // the output symbols function-LOCAL and DECLs them per invocation, so each
  // dynamic call gets fresh instances. The critical invariant is that EVERY
  // call site inside a function passes its scope: a scope-less call in a
  // twice-called function produced conflicting constraints over one global
  // symbol -> UNSAT -> every property vacuously SUCCESSFUL (a global false
  // proof; the boto3 factory-raise vacuity). emit_string_function therefore
  // takes in_loop/scope WITHOUT defaults, forcing call sites to decide.
  if(in_loop)
  {
    pending_checks.push_back(code_frontend_assignt{
      result.operands()[0],
      side_effect_expr_nondett{
        result.operands()[0].type(), source_locationt{}}});
    pending_checks.push_back(code_frontend_assignt{
      result.operands()[1],
      side_effect_expr_nondett{
        result.operands()[1].type(), source_locationt{}}});
  }

  std::vector<typet> arg_types;
  arg_types.push_back(signedbv_typet{64});
  arg_types.push_back(pointer_typet(unsignedbv_typet{8}, 64));
  for(const auto &a : extra_args)
    arg_types.push_back(a.type());

  irep_idt sym_id{func_id};
  if(symbol_table.lookup(sym_id) == nullptr)
  {
    symbolt fs{
      sym_id,
      mathematical_function_typet(std::move(arg_types), signedbv_typet{32}),
      "python"};
    fs.base_name = id2string(func_id);
    symbol_table.add(fs);
  }

  exprt::operandst args;
  args.push_back(result.operands()[0]);
  args.push_back(result.operands()[1]);
  args.insert(args.end(), extra_args.begin(), extra_args.end());

  function_application_exprt app(
    symbol_table.lookup_ref(sym_id).symbol_expr(), args);
  app.type() = signedbv_typet{32};

  // TU-safe uniquifier via symbol-table size (monotonic).
  std::size_t ctr = symbol_table.symbols.size();
  std::string rc_name = "__str_rc_" + std::to_string(ctr);
  irep_idt rc_id{"python::" + rc_name};
  if(symbol_table.lookup(rc_id) == nullptr)
  {
    symbolt rs{rc_id, signedbv_typet{32}, "python"};
    rs.base_name = rc_name;
    rs.is_lvalue = true;
    rs.is_state_var = true;
    symbol_table.add(rs);
  }
  pending_checks.push_back(
    code_frontend_assignt{symbol_table.lookup_ref(rc_id).symbol_expr(), app});

  return result;
}

/// Emit a one-string-argument int-returning intrinsic call (e.g.,
/// cprover_string_length_func).
[[maybe_unused]] static inline exprt emit_string_int_function(
  const irep_idt &func_id,
  const exprt &str,
  symbol_table_baset &symbol_table,
  std::vector<codet> &pending_checks)
{
  const typet int_type = signedbv_typet{64};
  // Direction B (native SMT-String backend): length lowers to the native-Int
  // (str.len s) via the shared upstream encoder. Declare the intrinsic with
  // its NATIVE Int result and assign it into the i64 result symbol through a
  // typecast, so the universal convert_typecast supplies the int2bv. Refined
  // struct operands keep the bit-vector convention -- the SAT string solver
  // PRECONDITIONs on the bit-vector length_type and cannot take a native Int.
  // (A run is backend-uniform, so the shared fn symbol's signature is
  // consistent within a run.)
  const bool native_length =
    str.type().id() == ID_string && func_id == ID_cprover_string_length_func;
  const typet app_result_type =
    native_length ? typet{integer_typet{}} : int_type;
  irep_idt sym_id{func_id};
  if(symbol_table.lookup(sym_id) == nullptr)
  {
    std::vector<typet> arg_types{str.type()};
    symbolt fs{
      sym_id,
      mathematical_function_typet(std::move(arg_types), app_result_type),
      "python"};
    fs.base_name = id2string(func_id);
    symbol_table.add(fs);
  }

  function_application_exprt app(
    symbol_table.lookup_ref(sym_id).symbol_expr(), {str});
  app.type() = app_result_type;

  std::size_t ctr = symbol_table.symbols.size();
  std::string rc_name = "__str_int_" + std::to_string(ctr);
  irep_idt rc_id{"python::" + rc_name};
  if(symbol_table.lookup(rc_id) == nullptr)
  {
    symbolt rs{rc_id, int_type, "python"};
    rs.base_name = rc_name;
    rs.is_lvalue = true;
    rs.is_state_var = true;
    symbol_table.add(rs);
  }
  // Soundness (front-end): bound the native Int length below 2^63 so the i64
  // int2bv read is faithful (see native_or_member_string_length). Only for the
  // native length op; str.len is deterministic so one assume suffices globally.
  if(native_length)
    pending_checks.push_back(code_assumet{binary_relation_exprt{
      app, ID_lt, from_integer(mp_integer{1} << 63, integer_typet{})}});
  exprt assigned = native_length ? exprt{typecast_exprt{app, int_type}}
                                 : exprt{std::move(app)};
  pending_checks.push_back(code_frontend_assignt{
    symbol_table.lookup_ref(rc_id).symbol_expr(), std::move(assigned)});
  return symbol_table.lookup_ref(rc_id).symbol_expr();
}

/// Build a string literal and register it with the string solver.
/// Uses ID_cprover_string_literal_func so the solver knows the content.
[[maybe_unused]] static inline exprt build_solver_string_literal(
  const std::string &s,
  symbol_table_baset &symbol_table,
  std::vector<codet> &pending_checks)
{
  constant_exprt lit_val{s, string_typet{}};
  // A literal's solver constraints are IDENTICAL on every execution (constant
  // content), so re-execution cannot conflict: global output symbols are safe
  // here (in_loop=false, no scope).
  return emit_string_function(
    ID_cprover_string_literal_func,
    {lit_val},
    symbol_table,
    pending_checks,
    false,
    std::string{});
}

/// Build a struct_exprt representing an inline string literal with a
/// backing char array of exactly s.size() bytes.
[[maybe_unused]] static inline exprt build_string_struct(const std::string &s)
{
  // Native SMT-String back-end (Plan A): a string is the SMT String sort, so
  // a compile-time literal is an opaque smt_string constant, not a {length,
  // data} struct. (Producing a struct_exprt with smt_string type here is
  // malformed and crashes downstream assignment/typecast handling.)
  if(python_smt_string_native_flag())
    return constant_exprt{irep_idt{s}, string_typet{}};
  exprt::operandst chars;
  for(char c : s)
    chars.push_back(
      from_integer(static_cast<unsigned char>(c), unsignedbv_typet{8}));
  array_typet at(
    unsignedbv_typet{8}, from_integer(chars.size(), signedbv_typet{64}));
  array_exprt arr(std::move(chars), at);
  exprt content = address_of_exprt(
    index_exprt(arr, from_integer(0, signedbv_typet{64}), unsignedbv_typet{8}));
  // PLR §3.6: 'len(s)' returns the number of code points, not
  // the number of UTF-8 bytes. Count code points by counting
  // bytes that are NOT continuation bytes (UTF-8 continuation
  // bytes have the form 10xxxxxx, i.e. value in [0x80, 0xC0)).
  long long codepoints = 0;
  for(char c : s)
  {
    unsigned char uc = static_cast<unsigned char>(c);
    if(uc < 0x80 || uc >= 0xC0)
      ++codepoints;
  }
  exprt length = from_integer(codepoints, signedbv_typet{64});
  return struct_exprt({length, content}, python_string_type());
}

/// Register a string expression with the string solver's array_pool.
[[maybe_unused]] static inline void register_string_with_solver(
  const exprt &str_expr,
  symbol_table_baset &symbol_table,
  std::vector<codet> &pending_checks)
{
  exprt length =
    (str_expr.id() == ID_struct && str_expr.operands().size() == 2)
      ? str_expr.operands()[0]
      : exprt(member_exprt(str_expr, "length", signedbv_typet{64}));
  exprt content =
    (str_expr.id() == ID_struct && str_expr.operands().size() == 2)
      ? str_expr.operands()[1]
      : exprt(member_exprt(
          str_expr, "data", pointer_typet(unsignedbv_typet{8}, 64)));

  std::size_t arr_ctr = symbol_table.symbols.size();
  std::string arr_name = "__str_arr_" + std::to_string(arr_ctr);
  irep_idt arr_id{"python::" + arr_name};
  array_typet inf_array_type(
    unsignedbv_typet{8}, infinity_exprt(signedbv_typet{64}));
  if(symbol_table.lookup(arr_id) == nullptr)
  {
    symbolt as{arr_id, inf_array_type, "python"};
    as.base_name = arr_name;
    as.is_lvalue = true;
    as.is_state_var = true;
    symbol_table.add(as);
  }
  exprt array_sym = symbol_table.lookup_ref(arr_id).symbol_expr();

  {
    irep_idt fn_id{ID_cprover_associate_array_to_pointer_func};
    if(symbol_table.lookup(fn_id) == nullptr)
    {
      std::vector<typet> arg_types{inf_array_type, content.type()};
      symbolt fs{
        fn_id,
        mathematical_function_typet(std::move(arg_types), signedbv_typet{32}),
        "python"};
      fs.base_name = id2string(fn_id);
      symbol_table.add(fs);
    }
    function_application_exprt app(
      symbol_table.lookup_ref(fn_id).symbol_expr(), {array_sym, content});
    app.type() = signedbv_typet{32};

    std::size_t rc_ctr = symbol_table.symbols.size();
    std::string rc_name = "__assoc_rc_" + std::to_string(rc_ctr);
    irep_idt rc_id{"python::" + rc_name};
    if(symbol_table.lookup(rc_id) == nullptr)
    {
      symbolt rs{rc_id, signedbv_typet{32}, "python"};
      rs.base_name = rc_name;
      rs.is_lvalue = true;
      rs.is_state_var = true;
      symbol_table.add(rs);
    }
    pending_checks.push_back(
      code_frontend_assignt{symbol_table.lookup_ref(rc_id).symbol_expr(), app});
  }

  {
    irep_idt fn_id{ID_cprover_associate_length_to_array_func};
    if(symbol_table.lookup(fn_id) == nullptr)
    {
      std::vector<typet> arg_types{inf_array_type, length.type()};
      symbolt fs{
        fn_id,
        mathematical_function_typet(std::move(arg_types), signedbv_typet{32}),
        "python"};
      fs.base_name = id2string(fn_id);
      symbol_table.add(fs);
    }
    function_application_exprt app(
      symbol_table.lookup_ref(fn_id).symbol_expr(), {array_sym, length});
    app.type() = signedbv_typet{32};

    std::size_t rc2_ctr = symbol_table.symbols.size();
    std::string rc_name = "__assoc_len_rc_" + std::to_string(rc2_ctr);
    irep_idt rc_id{"python::" + rc_name};
    if(symbol_table.lookup(rc_id) == nullptr)
    {
      symbolt rs{rc_id, signedbv_typet{32}, "python"};
      rs.base_name = rc_name;
      rs.is_lvalue = true;
      rs.is_state_var = true;
      symbol_table.add(rs);
    }
    pending_checks.push_back(
      code_frontend_assignt{symbol_table.lookup_ref(rc_id).symbol_expr(), app});
  }
}

#endif
