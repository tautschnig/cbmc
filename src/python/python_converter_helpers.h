/// Internal helper functions shared between python_converter.cpp
/// and its source-split siblings (python_converter_compare.cpp,
/// etc.). These are inline so each translation unit gets its own
/// copy and avoids multiple-definition link errors.
///
/// Kept out of python_converter.h because they are implementation
/// details, not part of the converter's public interface.

#ifndef CPROVER_PYTHON_CONVERTER_HELPERS_H
#define CPROVER_PYTHON_CONVERTER_HELPERS_H

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/json.h>
#include <util/mathematical_expr.h>
#include <util/mathematical_types.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/symbol.h>

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

#endif
