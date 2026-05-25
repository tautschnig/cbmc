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

#include <cstdint>
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
[[maybe_unused]] static inline exprt emit_string_bool_function(
  const irep_idt &func_id,
  const exprt &str1,
  const exprt &str2,
  symbol_table_baset &symbol_table,
  std::vector<codet> &pending_checks)
{
  const typet c_bool = c_bool_typet(8);
  irep_idt sym_id{func_id};
  if(symbol_table.lookup(sym_id) == nullptr)
  {
    std::vector<typet> arg_types;
    arg_types.push_back(str1.type());
    arg_types.push_back(str2.type());
    symbolt fs{
      sym_id,
      mathematical_function_typet(std::move(arg_types), c_bool),
      "python"};
    fs.base_name = id2string(func_id);
    symbol_table.add(fs);
  }

  function_application_exprt app(
    symbol_table.lookup_ref(sym_id).symbol_expr(), {str1, str2});
  app.type() = c_bool;

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
  pending_checks.push_back(
    code_frontend_assignt{symbol_table.lookup_ref(rc_id).symbol_expr(), app});

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
  std::map<std::string, std::set<std::string>> &out)
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
      if(type == "FunctionDef" || type == "AsyncFunctionDef" ||
         type == "Lambda")
        return;
      // Recognise direct alias assignments.
      if(type == "Assign")
      {
        const jsont &tgts = n["targets"];
        const jsont &val = n["value"];
        if(
          tgts.is_array() && val.is_object() &&
          val["_type"].value == "Name" &&
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
  std::function<void(const jsont &)> attr_pass = [&](const jsont &n)
  {
    if(n.is_null())
      return;
    if(n.is_object())
    {
      const std::string &type = n["_type"].value;
      if(type == "FunctionDef" || type == "AsyncFunctionDef" ||
         type == "Lambda")
        return;

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
              out[canonical].insert(attr);
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
[[maybe_unused]] static inline exprt
make_nondet_string(symbol_table_baset &symbol_table)
{
  // Uniquifier based on symbol-table size — monotonic across all
  // call sites in one converter instance, TU-safe (no per-TU
  // counter).
  std::size_t ctr = symbol_table.symbols.size();
  std::string len_name = "__string_len_" + std::to_string(ctr);
  std::string ptr_name = "__string_ptr_" + std::to_string(ctr);

  irep_idt len_id{"python::" + len_name};
  if(symbol_table.lookup(len_id) == nullptr)
  {
    symbolt ls{len_id, signedbv_typet{64}, "python"};
    ls.base_name = len_name;
    ls.is_lvalue = true;
    ls.is_state_var = true;
    symbol_table.add(ls);
  }

  irep_idt ptr_id{"python::" + ptr_name};
  if(symbol_table.lookup(ptr_id) == nullptr)
  {
    symbolt ps{ptr_id, pointer_typet(unsignedbv_typet{8}, 64), "python"};
    ps.base_name = ptr_name;
    ps.is_lvalue = true;
    ps.is_state_var = true;
    symbol_table.add(ps);
  }

  exprt len_expr = symbol_table.lookup_ref(len_id).symbol_expr();
  exprt ptr_expr = symbol_table.lookup_ref(ptr_id).symbol_expr();
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
  bool in_loop = false)
{
  exprt result = make_nondet_string(symbol_table);

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
  // (github_2992_lower) if applied unconditionally.
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
  irep_idt sym_id{func_id};
  if(symbol_table.lookup(sym_id) == nullptr)
  {
    std::vector<typet> arg_types{str.type()};
    symbolt fs{
      sym_id,
      mathematical_function_typet(std::move(arg_types), int_type),
      "python"};
    fs.base_name = id2string(func_id);
    symbol_table.add(fs);
  }

  function_application_exprt app(
    symbol_table.lookup_ref(sym_id).symbol_expr(), {str});
  app.type() = int_type;

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
  pending_checks.push_back(
    code_frontend_assignt{symbol_table.lookup_ref(rc_id).symbol_expr(), app});
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
  return emit_string_function(
    ID_cprover_string_literal_func, {lit_val}, symbol_table, pending_checks);
}

/// Build a struct_exprt representing an inline string literal with a
/// backing char array of exactly s.size() bytes.
[[maybe_unused]] static inline exprt build_string_struct(const std::string &s)
{
  exprt::operandst chars;
  for(char c : s)
    chars.push_back(
      from_integer(static_cast<unsigned char>(c), unsignedbv_typet{8}));
  array_typet at(
    unsignedbv_typet{8}, from_integer(chars.size(), signedbv_typet{64}));
  array_exprt arr(std::move(chars), at);
  exprt content = address_of_exprt(
    index_exprt(arr, from_integer(0, signedbv_typet{64}), unsignedbv_typet{8}));
  exprt length =
    from_integer(static_cast<long long>(s.size()), signedbv_typet{64});
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
