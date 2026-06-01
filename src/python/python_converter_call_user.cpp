/// Python to GOTO converter — user-function-call resolution
/// (PLR §4.2.1, §6.3.4). Handles nested function definitions,
/// lambdas, decorator wrappers, callable-instance __call__
/// dispatch, the @c_intrinsic redirection that delegates to
/// the corresponding C function, function aliases registered
/// via `register_function_alias`, and the keyword/vararg
/// argument binding that produces the final
/// side_effect_expr_function_callt. Extracted from
/// python_converter_call.cpp per
/// doc/python-frontend-call-refactor-plan.md (Phases 3.7 + 3.8).
/// Pure source-split — semantics are preserved.

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/config.h>
#include <util/cprover_prefix.h>
#include <util/floatbv_expr.h>
#include <util/ieee_float.h>
#include <util/json.h>
#include <util/mathematical_expr.h>
#include <util/mathematical_types.h>
#include <util/pointer_expr.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/std_types.h>
#include <util/string_constant.h>
#include <util/string_expr.h>
#include <util/symbol.h>

#include "python_converter.h"
#include "python_converter_helpers.h"
#include "python_types.h"
#include "python_value_type.h"

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

exprt python_convertert::convert_user_call(
  const jsont &expr,
  const std::string &func_name,
  const jsont &args)
{
  // PLR §4.2.1: nested function definitions live in their
  // enclosing function's scope. Look up the callee in the
  // current function scope first, then walk outward through
  // enclosing_functions, then fall back to module scope.
  // This lets two sibling functions each define `def f(...)`
  // without colliding (the symbol id encodes the parent
  // chain — see python_converter_defs.cpp).
  auto try_function_scope = [&](const std::string &scope) -> const symbolt *
  {
    irep_idt sid{
      scope.empty() ? std::string{"python::" + func_name}
                    : std::string{"python::" + scope + "::" + func_name}};
    const symbolt *s = symbol_table.lookup(sid);
    if(s != nullptr && s->type.id() == ID_code)
      return s;
    return nullptr;
  };
  irep_idt symbol_id;
  const symbolt *sym = nullptr;
  if(!current_function.empty())
  {
    if((sym = try_function_scope(current_function)) != nullptr)
      symbol_id = irep_idt{"python::" + current_function + "::" + func_name};
  }
  if(sym == nullptr)
  {
    for(auto it = enclosing_functions.rbegin();
        sym == nullptr && it != enclosing_functions.rend();
        ++it)
    {
      if((sym = try_function_scope(*it)) != nullptr)
        symbol_id = irep_idt{"python::" + *it + "::" + func_name};
    }
  }
  if(sym == nullptr)
  {
    if((sym = try_function_scope(std::string{})) != nullptr)
      symbol_id = irep_idt{"python::" + func_name};
  }
  // If no code symbol was found, fall back to the bare name
  // for the existing variable / class-instance / no-body paths.
  if(sym == nullptr)
  {
    symbol_id = irep_idt{"python::" + func_name};
    sym = symbol_table.lookup(symbol_id);
  }
  // The former ad-hoc math-function block has been retired.
  // math.py declares each function with @c_intrinsic('name',
  // fold='op', domain='kind', range='kind').
  // The decorator path a few hundred lines below handles the
  // complete semantics: parse-time fold for constants, guarded
  // ValueError for domain violations, and range-constrained
  // nondet return for symbolic arguments.

  // Check function aliases (lambda assignments: double = lambda x: x*2,
  // decorator application: @dec def f → f = dec(f), etc.)
  // PLR §8.7: decorators rebind the function name to the wrapper.
  // We check aliases FIRST so that a decorated function dispatches
  // through its wrapper even though the original symbol exists.
  {
    auto alias_it = function_aliases.find(qualify_name(func_name));
    if(alias_it != function_aliases.end())
    {
      const symbolt *alias_sym = symbol_table.lookup(alias_it->second);
      if(alias_sym != nullptr && alias_sym->type.id() == ID_code)
        sym = alias_sym;
    }
  }

  if(sym == nullptr || sym->type.id() != ID_code)
  {
    // Try module scope (forward references from inside functions)
    if(!current_function.empty())
    {
      sym = symbol_table.lookup(irep_idt{"python::" + func_name});
      if(sym != nullptr && sym->type.id() != ID_code)
        sym = nullptr;
    }
  }

  // PLR §3.3.5: callable instances via __call__. If the
  // 'function' is actually a symbol naming a class
  // instance (struct-typed), look up its __call__
  // method and emit a method call.
  if(sym == nullptr || sym->type.id() != ID_code)
  {
    const symbolt *var_sym =
      symbol_table.lookup(irep_idt{qualify_name(func_name)});
    if(var_sym == nullptr)
      var_sym = symbol_table.lookup(irep_idt{"python::" + func_name});
    if(var_sym != nullptr)
    {
      typet inst_type = var_sym->type;
      std::string tag;
      if(inst_type.id() == ID_struct)
        tag = id2string(to_struct_type(inst_type).get_tag());
      else if(inst_type.id() == ID_struct_tag)
        tag = id2string(to_struct_tag_type(inst_type).get_identifier());
      if(tag.substr(0, 13) == "python_class_")
      {
        std::string bare = tag.substr(13);
        for(const std::string &prefix :
            {std::string{"python::"} + tag + "::__call__",
             std::string{"python::"} + bare + "::__call__"})
        {
          const symbolt *cs = symbol_table.lookup(irep_idt{prefix});
          if(cs != nullptr)
          {
            typet ret_type = python_int_type();
            if(cs->type.id() == ID_code)
              ret_type = to_code_type(cs->type).return_type();
            exprt::operandst call_args;
            call_args.push_back(address_of_exprt{var_sym->symbol_expr()});
            if(args.is_array())
            {
              for(const auto &arg : as_array(args))
                call_args.push_back(convert_expression(arg));
            }
            return side_effect_expr_function_callt{
              cs->symbol_expr(),
              std::move(call_args),
              ret_type,
              get_location(expr)};
          }
        }
      }
    }
  }

  if(sym == nullptr || sym->type.id() != ID_code)
  {
    // Recognised-but-unmodelled Python built-ins. Returning a sound
    // nondet value is safe for these because (i) they are pure or
    // have no side effects that affect verification targets, and
    // (ii) any further reasoning about them would require a proper
    // model (tracked by Step 2 of the module support plan). By
    // whitelisting them here we avoid the spurious "no-body" false
    // positive that would otherwise fail verification of any
    // program that merely mentions them.
    //
    // Each entry maps the Python name to the type of the nondet
    // value returned. A nil typet means: use python_int_type().
    static const std::map<std::string, typet> known_nondet_builtins = {
      {"getattr", typet{}},
      {"setattr", typet{}},
      {"hasattr", bool_typet{}},
      {"callable", bool_typet{}},
      {"issubclass", bool_typet{}},
      {"id", typet{}},
      {"hash", typet{}},
      {"iter", typet{}},
      {"next", typet{}},
      {"tuple", typet{}},
      {"list", typet{}},
      {"set", typet{}},
      {"frozenset", typet{}},
      {"dict", typet{}},
      {"bytes", typet{}},
      {"bytearray", typet{}},
      {"memoryview", typet{}},
      {"super", typet{}},
      {"object", typet{}},
      {"classmethod", typet{}},
      {"staticmethod", typet{}},
      {"property", typet{}},
      {"vars", typet{}},
      {"dir", typet{}},
      {"globals", typet{}},
      {"locals", typet{}},
      {"open", typet{}},
      {"type", typet{}},
    };
    auto builtin_it = known_nondet_builtins.find(func_name);
    if(builtin_it != known_nondet_builtins.end())
    {
      // A default-constructed typet in the table means "use the
      // generic python_int_type()"; otherwise use the explicit type.
      // typet{}.is_nil() is false (the default id is empty, not
      // ID_nil), so check for an empty id instead.
      const typet t = builtin_it->second.id().empty() ? python_int_type()
                                                      : builtin_it->second;
      side_effect_expr_nondett nondet{t, get_location(expr)};
      return std::move(nondet);
    }

    // Unknown function — return nondet value (sound overapproximation).
    // Quiet by default because stdlib ingestion routinely hits
    // hundreds of these and the fallback is already correct (nondet
    // + optional no-body assertion). Visible with --verbosity 9 or
    // --python-strict-warnings.
    log_overapprox(
      "function '" + func_name +
      "': no body known, returning nondet over-approximation");
    // Suppress the no-body property for names that came from a
    // failed-to-resolve import. The user can't provide a body
    // for them and the tool can't be sound about their
    // behaviour; treat the call as nondet (sound
    // over-approximation) without flagging a property.
    if(!no_body_check && unresolved_imports.count(func_name) == 0)
    {
      add_check(
        false_exprt{},
        "no-body",
        "no body for callee " + func_name,
        get_location(expr));
    }
    side_effect_expr_nondett nondet{python_int_type(), get_location(expr)};
    return std::move(nondet);
  }

  const code_typet &func_type = to_code_type(sym->type);
  const auto &params = func_type.parameters();

  // Build argument list: start with positional args
  exprt::operandst arguments;
  if(args.is_array())
  {
    for(const auto &arg : as_array(args))
    {
      // PEP 448: f(*c) — spread known list literals at
      // conversion time. Non-literal iterables are
      // over-approximated by appending a single nondet.
      if(is_node_type(arg, "Starred"))
      {
        exprt inner = convert_expression(json_member(arg, "value"));
        if(inner.is_nil())
        {
          arguments.push_back(nil_exprt{});
          continue;
        }
        const exprt *lit = nullptr;
        if(inner.id() == ID_struct && inner.operands().size() >= 2)
          lit = &inner;
        else if(inner.id() == ID_symbol)
        {
          auto it = list_literals.find(to_symbol_expr(inner).get_identifier());
          if(it != list_literals.end())
            lit = &it->second;
        }
        if(
          lit != nullptr && lit->operands().size() >= 2 &&
          lit->operands()[0].is_constant())
        {
          mp_integer len_val;
          if(!to_integer(to_constant_expr(lit->operands()[0]), len_val))
          {
            const exprt &data_arr = lit->operands()[1];
            std::size_t n = len_val.to_ulong();
            for(std::size_t i = 0; i < n && i < data_arr.operands().size(); i++)
              arguments.push_back(data_arr.operands()[i]);
            continue;
          }
        }
        // PLR §8.7: f(*xs) where xs is a list-typed expression
        // (e.g. a parameter, not a literal). The target
        // function's arity tells us how many elements to read
        // from the list. Generate xs.data[0], xs.data[1], ...,
        // xs.data[target_arity-1] for the remaining slots.
        // If the list is shorter at run time, the trailing
        // reads return zero (the list buffer is fixed-size and
        // zero-initialised), which over-approximates a Python
        // TypeError-at-runtime as a precision-loss but is sound
        // for the common case where the caller correctly forwards.
        if(is_python_list_type(inner.type()))
        {
          // Number of remaining positional slots in the target
          // (params already provided so far excluded).
          std::size_t already = arguments.size();
          // Target arity: regular params (non-vararg). If the
          // target's last param is itself list-typed (*args), we
          // expand up to that param and let downstream packing
          // re-pack the rest.
          std::size_t remaining = 0;
          if(params.size() > already)
            remaining = params.size() - already;
          // Cap at PYTHON_MAX_LIST_LENGTH so we don't read past
          // the buffer.
          if(remaining > PYTHON_MAX_LIST_LENGTH)
            remaining = PYTHON_MAX_LIST_LENGTH;
          if(remaining > 0)
          {
            const auto &list_st = to_struct_type(inner.type());
            const auto &data_type =
              to_array_type(list_st.components()[1].type());
            exprt data_member = member_exprt{inner, "data", data_type};
            for(std::size_t i = 0; i < remaining; i++)
            {
              exprt idx =
                from_integer(static_cast<long long>(i), signedbv_typet{64});
              arguments.push_back(
                index_exprt{data_member, idx, data_type.element_type()});
            }
            continue;
          }
        }
        log_overapprox(
          "PEP 448 call unpacking of non-literal iterable — nondet");
        arguments.push_back(
          side_effect_expr_nondett{python_int_type(), source_locationt{}});
        continue;
      }
      arguments.push_back(convert_expression(arg));
    }
  }

  // PLR §8.7: pack *args BEFORE keyword handling. The keyword loop
  // resizes arguments to params.size() and writes kwarg values to
  // their named slots, including kwonly slots after *args. If we
  // packed AFTER, the kwonly slot's value would be swept into the
  // *args list (wrong) or the positionals at *args slot would be
  // overwritten by later kwarg writes. Packing first leaves a
  // single packed-list entry at va_idx and nil placeholders at
  // kwonly slots, which the keyword loop then fills cleanly.
  {
    auto va_it_pre = function_vararg_index.find(sym->name);
    if(
      va_it_pre != function_vararg_index.end() &&
      va_it_pre->second < params.size())
    {
      std::size_t va_idx = va_it_pre->second;
      const auto &va_param_type = params[va_idx].type();
      if(is_python_list_type(va_param_type))
      {
        const auto &list_st = to_struct_type(va_param_type);
        const auto &data_type = to_array_type(list_st.components()[1].type());
        exprt::operandst elems;
        for(std::size_t i = va_idx; i < arguments.size(); i++)
        {
          exprt arg = arguments[i];
          if(arg.is_nil())
            continue;
          if(arg.type() != data_type.element_type())
            arg = coerce_element(arg, data_type.element_type());
          elems.push_back(arg);
        }
        std::size_t n_packed = elems.size();
        while(elems.size() < PYTHON_MAX_LIST_LENGTH)
          elems.push_back(safe_zero(data_type.element_type()));
        exprt length =
          from_integer(static_cast<long long>(n_packed), signedbv_typet{64});
        exprt packed = struct_exprt{
          {length, array_exprt{std::move(elems), data_type}}, va_param_type};
        arguments.resize(va_idx);
        arguments.push_back(std::move(packed));
        // Pad to params.size() so kwarg loop can index kwonly slots.
        while(arguments.size() < params.size())
          arguments.push_back(nil_exprt{});
      }
    }
  }

  // Handle keyword arguments: match by parameter name
  const jsont &keywords = json_member(expr, "keywords");
  if(keywords.is_array() && !as_array(keywords).empty())
  {
    arguments.resize(params.size(), nil_exprt{});

    // Collect unmatched keywords for **kwargs
    std::vector<std::pair<std::string, exprt>> unmatched_kw;

    for(const auto &kw : as_array(keywords))
    {
      std::string kw_name = json_string(json_member(kw, "arg"));
      exprt kw_val = convert_expression(json_member(kw, "value"));

      // PEP 448: f(**d) — kw_name is empty. Spread the dict's
      // known keys into individual entries. If the dict's
      // content isn't known at conversion time, register a
      // single nondet entry under the name '*' to signal
      // 'kwargs may have arbitrary extra keys'.
      if(kw_name.empty())
      {
        const exprt *lit = nullptr;
        if(kw_val.id() == ID_struct && kw_val.operands().size() >= 3)
          lit = &kw_val;
        else if(kw_val.id() == ID_symbol)
        {
          auto it = dict_literals.find(to_symbol_expr(kw_val).get_identifier());
          if(it != dict_literals.end())
            lit = &it->second;
        }
        if(
          lit != nullptr && lit->operands().size() >= 3 &&
          lit->operands()[0].is_constant())
        {
          mp_integer len_val;
          if(!to_integer(to_constant_expr(lit->operands()[0]), len_val))
          {
            const exprt &keys_arr = lit->operands()[1];
            const exprt &vals_arr = lit->operands()[2];
            std::size_t n = len_val.to_ulong();
            for(std::size_t i = 0; i < n && i < keys_arr.operands().size(); i++)
            {
              auto kv = extract_string_value(keys_arr.operands()[i]);
              if(!kv.has_value())
                continue;
              // Try matching against params, else add to
              // unmatched_kw for packing.
              bool matched = false;
              for(std::size_t j = 0; j < params.size(); j++)
              {
                if(id2string(params[j].get_base_name()) == kv.value())
                {
                  // Typecast spread value to declared param type
                  // (see comment in the method-call branch above).
                  exprt v = vals_arr.operands()[i];
                  v = coerce_call_argument(v, params[j].type());
                  arguments[j] = std::move(v);
                  matched = true;
                  break;
                }
              }
              if(!matched)
                unmatched_kw.push_back({kv.value(), vals_arr.operands()[i]});
            }
            continue;
          }
        }
        // Unknown dict contents: skip (over-approx — kwargs
        // will miss these, required-kwarg checks may FP).
        log_overapprox(
          "**dict spread of non-literal dict — kwargs left under-populated");
        continue;
      }

      bool matched = false;
      for(std::size_t i = 0; i < params.size(); i++)
      {
        if(id2string(params[i].get_base_name()) == kw_name)
        {
          exprt v = kw_val;
          v = coerce_call_argument(v, params[i].type());
          arguments[i] = std::move(v);
          matched = true;
          break;
        }
      }
      if(!matched)
        unmatched_kw.push_back({kw_name, kw_val});
    }

    // Pack unmatched keywords into a dict for the last param if it's a dict
    if(
      !unmatched_kw.empty() && !params.empty() &&
      is_python_dict_type(params.back().type()))
    {
      std::size_t kwargs_idx = params.size() - 1;
      typet dict_type = params.back().type();
      const auto &dict_st = to_struct_type(dict_type);
      const auto &keys_arr_type = to_array_type(dict_st.components()[1].type());
      const auto &vals_arr_type = to_array_type(dict_st.components()[2].type());

      exprt::operandst key_elems, val_elems;
      for(const auto &[name, val] : unmatched_kw)
      {
        key_elems.push_back(python_string_literal(name));
        if(is_python_value_type(vals_arr_type.element_type()))
          val_elems.push_back(wrap_value(val));
        else
          val_elems.push_back(
            coerce_element(val, vals_arr_type.element_type()));
      }
      while(key_elems.size() < PYTHON_MAX_DICT_SIZE)
      {
        key_elems.push_back(safe_zero(keys_arr_type.element_type()));
        val_elems.push_back(safe_zero(vals_arr_type.element_type()));
      }
      exprt length = from_integer(
        static_cast<long long>(unmatched_kw.size()), signedbv_typet{64});
      arguments[kwargs_idx] = struct_exprt{
        {length,
         array_exprt{std::move(key_elems), keys_arr_type},
         array_exprt{std::move(val_elems), vals_arr_type}},
        dict_type};
    }
  }

  // Prepend bound self for bound method calls
  {
    auto bm_it = bound_methods.find(qualify_name(func_name));
    if(bm_it != bound_methods.end())
    {
      arguments.insert(arguments.begin(), bm_it->second.second);
    }
  }

  // Add closure captures as extra arguments BEFORE padding
  {
    auto cap_it2 = closure_captures.find(id2string(sym->name));
    if(cap_it2 != closure_captures.end())
    {
      for(const auto &[outer_id, name, type] : cap_it2->second)
      {
        const symbolt *outer_sym = symbol_table.lookup(irep_idt{outer_id});
        if(outer_sym != nullptr)
          arguments.push_back(outer_sym->symbol_expr());
        else
          arguments.push_back(
            side_effect_expr_nondett{type, get_location(expr)});
      }
    }
  }

  // Fill in defaults for any remaining nil arguments.
  // Defaults are stored in the FunctionDef AST; look up the function's
  // definition to find them.
  if(arguments.size() < params.size())
    arguments.resize(params.size(), nil_exprt{});

  // Use pre-evaluated default values (evaluated at definition time)
  for(std::size_t i = 0; i < arguments.size() && i < params.size(); i++)
  {
    if(arguments[i].is_nil())
    {
      auto def_it = default_values.find({func_name, i});
      if(def_it != default_values.end())
      {
        arguments[i] = def_it->second;
        // PLR §3.6: a frozen python_value{NONE} default for an
        // Optional[str] parameter binds as the canonical
        // length-0 marker so that 'y is None' (Optional[str])
        // and 'len(y) == 0' both evaluate correctly. The
        // symbol's stored value is python_value{NONE} but the
        // param expects python_string.
        if(
          is_python_value_type(arguments[i].type()) &&
          is_python_string_type(params[i].type()))
        {
          arguments[i] = struct_exprt{
            {from_integer(0, signedbv_typet{64}),
             null_pointer_exprt{pointer_typet{unsignedbv_typet{8}, 64}}},
            python_string_type()};
        }
        // Class reference: struct default → pointer param
        if(
          params[i].type().id() == ID_pointer &&
          arguments[i].type().id() == ID_struct &&
          to_pointer_type(params[i].type()).base_type() == arguments[i].type())
        {
          arguments[i] = address_of_exprt{arguments[i]};
        }
      }
    }
  }

  // Fallback: look up the function's AST to get defaults
  const jsont &body = json_member(parse_tree.ast_json, "body");
  if(body.is_array())
  {
    // Helper: try to bind defaults from a FunctionDef whose
    // name matches func_name. Returns true once a match is
    // processed so the outer scan can stop. Used both for
    // top-level defs and class methods (which live inside a
    // ClassDef body).
    auto try_apply_defaults = [&](const jsont &fn_stmt) -> bool
    {
      const jsont &func_args = json_member(fn_stmt, "args");
      const jsont &defaults = json_member(func_args, "defaults");
      if(!defaults.is_array())
        return true;
      std::size_t n_defaults = as_array(defaults).size();
      std::size_t first_default = params.size() - n_defaults;
      auto def_it = as_array(defaults).begin();
      for(std::size_t i = first_default; i < params.size(); i++, ++def_it)
      {
        if(arguments[i].is_nil())
        {
          arguments[i] = convert_expression(*def_it);
          // PLR §3.6: 'None' default for a python_string-typed
          // param (Optional[str] = None) needs to bind as a
          // distinguishable empty-string struct (length=0,
          // data=NULL) rather than the int sentinel that the
          // type-mismatch typecast turns into nondet. The
          // length=0 marker lets `y is None` (when y is in
          // optional_params, falling through to struct-vs-int
          // compare) recognise the default-None binding via
          // the length-zero discriminator.
          if(
            is_python_none_constant(arguments[i]) &&
            is_python_string_type(params[i].type()))
          {
            arguments[i] = struct_exprt{
              {from_integer(0, signedbv_typet{64}),
               null_pointer_exprt{pointer_typet{unsignedbv_typet{8}, 64}}},
              python_string_type()};
          }
          if(
            params[i].type().id() == ID_pointer &&
            arguments[i].type().id() == ID_struct &&
            to_pointer_type(params[i].type()).base_type() ==
              arguments[i].type())
          {
            arguments[i] = address_of_exprt{arguments[i]};
          }
        }
      }
      return true;
    };
    bool done = false;
    for(const auto &stmt : as_array(body))
    {
      if(done)
        break;
      if(
        (is_node_type(stmt, "FunctionDef") ||
         is_node_type(stmt, "AsyncFunctionDef")) &&
        json_string(json_member(stmt, "name")) == func_name)
      {
        done = try_apply_defaults(stmt);
        break;
      }
      // Class methods: walk the ClassDef body for matching
      // method definitions. Recognises 'ClassName.foo' lookups
      // too (qualified_func_name shape).
      if(is_node_type(stmt, "ClassDef"))
      {
        const jsont &cls_body = json_member(stmt, "body");
        if(cls_body.is_array())
        {
          for(const auto &cs : as_array(cls_body))
          {
            if(
              (is_node_type(cs, "FunctionDef") ||
               is_node_type(cs, "AsyncFunctionDef")) &&
              json_string(json_member(cs, "name")) == func_name)
            {
              done = try_apply_defaults(cs);
              break;
            }
          }
        }
      }
    }
  }

  // Replace any remaining nil arguments with safe_zero of param type
  for(std::size_t i = 0; i < arguments.size() && i < params.size(); i++)
  {
    if(arguments[i].is_nil())
    {
      if(params[i].type().id() == ID_pointer)
      {
        // For pointer params (class references), create a temp object
        static unsigned ref_tmp_ctr = 0;
        std::string tn = "__ref_tmp_" + std::to_string(ref_tmp_ctr++);
        std::string tq = qualify_name(tn);
        irep_idt ti{tq};
        typet base = to_pointer_type(params[i].type()).base_type();
        if(symbol_table.lookup(ti) == nullptr)
        {
          symbolt ts{ti, base, "python"};
          ts.base_name = tn;
          ts.is_lvalue = true;
          ts.is_state_var = true;
          symbol_table.add(ts);
        }
        arguments[i] =
          address_of_exprt{symbol_table.lookup_ref(ti).symbol_expr()};
      }
      else
        arguments[i] = safe_zero(params[i].type());
    }
  }

  // Remove trailing nil arguments beyond param count
  while(!arguments.empty() && arguments.back().is_nil())
    arguments.pop_back();

  // PLR §8.7: *args packing — the primary packing happens
  // BEFORE keyword handling (above). Only the legacy "trailing
  // list param without function_vararg_index" fallback runs
  // here; functions with a recorded vararg index already had
  // their *args packed.
  {
    auto va_it = function_vararg_index.find(sym->name);
    if(va_it != function_vararg_index.end() && va_it->second < params.size())
    {
      // Already packed by the early pass — nothing to do.
    }
    else if(arguments.size() > params.size() && !params.empty())
    {
      // Fallback for the legacy "trailing list param" case where
      // function_vararg_index wasn't populated (e.g. dynamically
      // synthesised functions). Same as before: if the LAST param
      // is list-typed, pack trailing positionals into it.
      const auto &last_param_type = params.back().type();
      if(is_python_list_type(last_param_type))
      {
        std::size_t n_regular = params.size() - 1;
        const auto &list_st = to_struct_type(last_param_type);
        const auto &data_type = to_array_type(list_st.components()[1].type());
        exprt::operandst elems;
        for(std::size_t i = n_regular; i < arguments.size(); i++)
        {
          exprt arg = arguments[i];
          if(arg.type() != data_type.element_type())
            arg = coerce_element(arg, data_type.element_type());
          elems.push_back(arg);
        }
        std::size_t n_packed = elems.size();
        while(elems.size() < PYTHON_MAX_LIST_LENGTH)
          elems.push_back(safe_zero(data_type.element_type()));
        exprt length =
          from_integer(static_cast<long long>(n_packed), signedbv_typet{64});
        exprt packed = struct_exprt{
          {length, array_exprt{std::move(elems), data_type}}, last_param_type};
        arguments.resize(n_regular);
        arguments.push_back(std::move(packed));
      }
    }
  }
  // Empty-args fallback: if we have a vararg param and no
  // positionals reached its slot, pass an empty list.
  {
    auto va_it = function_vararg_index.find(sym->name);
    if(va_it != function_vararg_index.end() && va_it->second < params.size())
    {
      std::size_t va_idx = va_it->second;
      if(va_idx >= arguments.size() || arguments[va_idx].is_nil())
      {
        const auto &va_param_type = params[va_idx].type();
        if(is_python_list_type(va_param_type))
        {
          const auto &list_st = to_struct_type(va_param_type);
          const auto &data_type = to_array_type(list_st.components()[1].type());
          exprt::operandst elems;
          while(elems.size() < PYTHON_MAX_LIST_LENGTH)
            elems.push_back(safe_zero(data_type.element_type()));
          exprt length = from_integer(0LL, signedbv_typet{64});
          exprt packed = struct_exprt{
            {length, array_exprt{std::move(elems), data_type}}, va_param_type};
          while(arguments.size() < va_idx)
            arguments.push_back(nil_exprt{});
          if(arguments.size() == va_idx)
            arguments.push_back(std::move(packed));
          else
            arguments[va_idx] = std::move(packed);
          while(arguments.size() < params.size())
            arguments.push_back(nil_exprt{});
        }
      }
    }
  }

  // Typecast arguments to match parameter types
  for(std::size_t i = 0; i < arguments.size() && i < params.size(); i++)
  {
    if(!arguments[i].is_nil() && arguments[i].type() != params[i].type())
    {
      // Class / list / dict reference: struct arg → pointer param.
      // address_of needs an lvalue, so symbol_exprt arguments are
      // taken-address-of directly. Literal struct_exprt or other
      // rvalues are first materialized into a fresh local
      // (pending_checks-staged so the materialization fires before
      // the call) so address_of has something to point at.
      //
      // We accept any pointer-to-struct param type. Element-type
      // mismatches between the argument's struct (e.g. list[str])
      // and the parameter's pointee struct (e.g. list[int]) are
      // bridged with a final typecast on the address_of, matching
      // the existing safe_typecast struct->pointer path that
      // CBMC's symex chases through dereferences.
      if(
        params[i].type().id() == ID_pointer &&
        arguments[i].type().id() == ID_struct &&
        to_pointer_type(params[i].type()).base_type().id() == ID_struct)
      {
        // P3: bare 'list' annotation widens to list[Any]
        // (python_value-element). When the caller's list has a
        // different element type (list[int], list[str], ...),
        // a plain pointer reinterpret would mis-read the
        // elements: python_string and python_value structs are
        // different sizes / shapes. Build a new list where each
        // element is wrap_value(orig_elem) so the callee sees
        // tagged values it can dispatch on (isinstance(x, str),
        // etc.).
        const typet &param_pointee =
          to_pointer_type(params[i].type()).base_type();
        // PLR §3.1 write-back: when the argument is an lvalue
        // symbol and we are about to build a element-type-promoted
        // COPY to pass by reference, remember the original storage
        // and its element type so we can copy the callee's
        // mutations back after the call.
        exprt writeback_target = nil_exprt{};
        typet writeback_elem_t;
        if(
          is_python_list_type(param_pointee) &&
          is_python_list_type(arguments[i].type()) &&
          arguments[i].type() != param_pointee &&
          arguments[i].id() == ID_symbol &&
          is_python_value_type(
            to_array_type(to_struct_type(param_pointee).components()[1].type())
              .element_type()))
        {
          writeback_target = arguments[i];
          writeback_elem_t =
            to_array_type(
              to_struct_type(arguments[i].type()).components()[1].type())
              .element_type();
        }
        if(
          is_python_list_type(param_pointee) &&
          is_python_list_type(arguments[i].type()) &&
          arguments[i].type() != param_pointee &&
          is_python_value_type(
            to_array_type(to_struct_type(param_pointee).components()[1].type())
              .element_type()))
        {
          const auto &src_st = to_struct_type(arguments[i].type());
          const auto &src_data_t = to_array_type(src_st.components()[1].type());
          const auto &dst_st = to_struct_type(param_pointee);
          const auto &dst_data_t = to_array_type(dst_st.components()[1].type());
          // Materialise the source so we can index into it.
          exprt src_struct = arguments[i];
          if(src_struct.id() != ID_symbol)
          {
            static unsigned src_mat_ctr = 0;
            std::string tn =
              "__list_promote_src_" + std::to_string(src_mat_ctr++);
            std::string tq = qualify_name(tn);
            irep_idt ti{tq};
            if(symbol_table.lookup(ti) == nullptr)
            {
              symbolt ts{ti, src_struct.type(), "python"};
              ts.base_name = tn;
              ts.is_lvalue = true;
              ts.is_state_var = true;
              ts.is_static_lifetime = current_function.empty();
              symbol_table.add(ts);
            }
            symbol_exprt s_sym = symbol_table.lookup_ref(ti).symbol_expr();
            pending_checks.push_back(code_frontend_assignt{s_sym, src_struct});
            src_struct = s_sym;
          }
          member_exprt src_len{src_struct, "length", signedbv_typet{64}};
          member_exprt src_data{src_struct, "data", src_data_t};
          // Build the promoted list element-wise.
          exprt::operandst promoted_elems;
          std::size_t max_len =
            static_cast<std::size_t>(PYTHON_MAX_LIST_LENGTH);
          for(std::size_t k = 0; k < max_len; k++)
          {
            exprt idx = from_integer(k, signedbv_typet{64});
            exprt orig = index_exprt{src_data, idx};
            // wrap_value lifts numerics / strings / etc. into
            // python_value tagged form. For elements at indices
            // beyond src_len the value is undefined but we
            // never read it (callee guards with i < length).
            promoted_elems.push_back(wrap_value(orig));
          }
          exprt promoted = struct_exprt{
            {src_len, array_exprt{std::move(promoted_elems), dst_data_t}},
            param_pointee};
          arguments[i] = std::move(promoted);
        }
        exprt addressable = arguments[i];
        if(addressable.id() != ID_symbol)
        {
          static unsigned mat_ctr = 0;
          std::string tn = "__byref_arg_" + std::to_string(mat_ctr++);
          std::string tq = qualify_name(tn);
          irep_idt ti{tq};
          if(symbol_table.lookup(ti) == nullptr)
          {
            symbolt ts{ti, addressable.type(), "python"};
            ts.base_name = tn;
            ts.is_lvalue = true;
            ts.is_state_var = true;
            ts.is_static_lifetime = current_function.empty();
            symbol_table.add(ts);
          }
          symbol_exprt mat_sym = symbol_table.lookup_ref(ti).symbol_expr();
          pending_checks.push_back(code_frontend_assignt{mat_sym, addressable});
          addressable = mat_sym;
        }
        exprt addr = address_of_exprt{addressable};
        if(addr.type() != params[i].type())
          addr = typecast_exprt{addr, params[i].type()};
        arguments[i] = std::move(addr);
        // PLR §3.1 write-back: copy the callee's mutations on the
        // promoted by-ref copy back into the caller's original
        // storage after the call. `addressable` is the
        // list[python_value] byref symbol the callee mutates;
        // `writeback_target` is the caller's original list[T]
        // lvalue. For each slot k: original.data[k] =
        // unwrap_value(byref.data[k], T); and original.length =
        // byref.length. Without this, mutations (append, element
        // assign) performed through the parameter were invisible
        // to the caller, producing spurious assertion failures on
        // correct programs (e.g. `def f(l): l.append(x); f(a);
        // assert len(a) == ...`).
        if(
          writeback_target.id() == ID_symbol && addressable.id() == ID_symbol &&
          is_python_list_type(addressable.type()))
        {
          const auto &by_st = to_struct_type(addressable.type());
          const auto &by_data_t = to_array_type(by_st.components()[1].type());
          member_exprt by_data{addressable, "data", by_data_t};
          member_exprt by_len{addressable, "length", signedbv_typet{64}};
          const auto &tgt_st = to_struct_type(writeback_target.type());
          const auto &tgt_data_t = to_array_type(tgt_st.components()[1].type());
          member_exprt tgt_data{writeback_target, "data", tgt_data_t};
          member_exprt tgt_len{writeback_target, "length", signedbv_typet{64}};
          for(std::size_t k = 0; k < PYTHON_MAX_LIST_LENGTH; k++)
          {
            exprt idx = from_integer(k, signedbv_typet{64});
            exprt elem =
              unwrap_value(index_exprt{by_data, idx}, writeback_elem_t);
            if(elem.type() != writeback_elem_t)
              elem = safe_typecast(elem, writeback_elem_t);
            pending_post_checks.push_back(
              code_frontend_assignt{index_exprt{tgt_data, idx}, elem});
          }
          pending_post_checks.push_back(code_frontend_assignt{tgt_len, by_len});
        }
        // PLR §3.1: passing a mutable container by reference means
        // the callee may mutate it. Invalidate the literal cache so
        // subsequent reads at the call site don't constant-fold
        // against the pre-call snapshot.
        if(addressable.id() == ID_symbol)
        {
          irep_idt sid = to_symbol_expr(addressable).get_identifier();
          list_literals.erase(sid);
          dict_literals.erase(sid);
          tuple_literals.erase(sid);
          string_constants.erase(sid);
          float_constants.erase(sid);
        }
      }
      else
      {
        // PLR soundness: emit annotation-mismatch property at
        // the call site when the argument's type is obviously
        // incompatible with the parameter's declared type.
        //
        // Skip when the parameter is list-typed and argument is
        // not — this is almost certainly a vararg (*args)
        // collection, not an annotation mismatch.
        bool is_likely_vararg_collect =
          is_python_list_type(params[i].type()) &&
          !is_python_list_type(arguments[i].type());
        if(
          python_check_annotations && !is_likely_vararg_collect &&
          (annotation_types_incompatible(
             params[i].type(), arguments[i].type()) ||
           union_annotation_violated(params[i].get_identifier(), arguments[i])))
        {
          add_check(
            false_exprt{},
            "annotation-mismatch",
            "argument " + std::to_string(i) +
              "'s type does not match declared parameter type of '" +
              func_name + "'",
            get_location(expr));
        }
        // Any-erasure detection: when the parameter is Any-typed
        // (python_value_type) and the caller's argument has a
        // known concrete class type, look up the function's
        // collected `param.X` references in
        // `function_param_attr_uses`. For each X that is NOT a
        // method on the argument's class, emit an
        // attribute-error property anchored at the call site.
        if(
          python_check_any_arg_attrs &&
          is_python_value_type(params[i].type()) &&
          (arguments[i].type().id() == ID_struct ||
           arguments[i].type().id() == ID_struct_tag))
        {
          // Resolve the class tag.
          std::string arg_class_tag;
          if(arguments[i].type().id() == ID_struct_tag)
          {
            const auto &tag = to_struct_tag_type(arguments[i].type());
            const symbolt *sym = symbol_table.lookup(tag.get_identifier());
            if(sym != nullptr && sym->type.id() == ID_struct)
              arg_class_tag = id2string(to_struct_type(sym->type).get_tag());
          }
          else
            arg_class_tag =
              id2string(to_struct_type(arguments[i].type()).get_tag());
          // Look up the function's parameter name. params[i] has
          // identifier "python::<func>::<param_name>".
          irep_idt param_id = params[i].get_identifier();
          auto it = function_param_attr_uses.find(param_id);
          if(it != function_param_attr_uses.end() && !arg_class_tag.empty())
          {
            // Strip "python_class_" prefix if present.
            std::string class_name = arg_class_tag;
            if(class_name.rfind("python_class_", 0) == 0)
              class_name = class_name.substr(13);
            // PLR §3.2: skip the missing-method check for
            // builtin container types. Their methods (.items(),
            // .keys(), .append(), .upper(), etc.) are dispatched
            // by call_method and are NOT recorded in
            // class_declared_methods. Without this skip, every
            // 'def f(d): for k, v in d.items(): ...' call with
            // a concrete dict argument would fire a false
            // attribute-error.
            static const std::set<std::string> builtin_container_tags = {
              "python_dict_array",
              "python_list",
              "python_string",
              "python_set",
              "python_tuple",
              "python_complex"};
            if(builtin_container_tags.count(arg_class_tag) > 0)
              goto skip_any_attr_check;
            auto cdm = class_declared_methods.find(class_name);
            // Boto3 base methods (inherited helpers) — do not
            // flag these even if absent from the class's own
            // declared methods.
            static const std::set<std::string> boto3_base_methods{
              "get_paginator",
              "can_paginate",
              "get_waiter",
              "close",
              "exceptions",
              "meta",
              "generate_presigned_url",
              "generate_presigned_post"};
            for(const auto &attr_name : it->second)
            {
              if(boto3_base_methods.count(attr_name) > 0)
                continue;
              // PLR §3.3.5: isinstance-narrowing gate. If
              // every recorded access of this attribute is
              // gated by `if isinstance(param, GateClass):`
              // and the caller's argument class is NOT in
              // any of the gates, the access is unreachable
              // at runtime and we should not flag it.
              auto gates_it = function_param_attr_gates.find(param_id);
              if(gates_it != function_param_attr_gates.end())
              {
                auto attr_gates = gates_it->second.find(attr_name);
                if(attr_gates != gates_it->second.end())
                {
                  const auto &gset = attr_gates->second;
                  // Only narrow when the gate set is non-empty
                  // AND doesn't contain the empty-string
                  // ungated marker.
                  if(!gset.empty() && gset.count(std::string{}) == 0)
                  {
                    if(gset.count(class_name) == 0)
                      continue; // gate excludes this arg class
                  }
                }
              }
              bool found = false;
              if(
                cdm != class_declared_methods.end() &&
                cdm->second.count(attr_name) > 0)
                found = true;
              // Also accept class-level fields declared in
              // the class's struct (e.g. `year: int` on
              // datetime). The Any-typed-parameter analysis
              // only collected attribute names without
              // distinguishing field-vs-method use, so a
              // bare `obj.year` would erroneously land in
              // the "missing method" path.
              if(!found)
              {
                auto ct = class_types.find(class_name);
                if(ct != class_types.end())
                {
                  for(const auto &comp : ct->second.components())
                  {
                    if(id2string(comp.get_name()) == attr_name)
                    {
                      found = true;
                      break;
                    }
                  }
                }
              }
              if(!found)
              {
                add_check(
                  false_exprt{},
                  "attribute-error",
                  "argument " + std::to_string(i) + " of class '" + class_name +
                    "' missing method '" + attr_name +
                    "' referenced via Any-typed parameter '" +
                    id2string(params[i].get_base_name()) + "' in '" +
                    func_name + "'",
                  get_location(expr));
              }
            }
          }
        skip_any_attr_check:;
        }
        arguments[i] = coerce_call_argument(arguments[i], params[i].type());
      }
    }
  }

  // @c_intrinsic: redirect the call to the named C function. The
  // Python function's declared signature is used as-is for the C
  // intrinsic, with one exception — Python str parameters (and
  // str returns) are marshalled to/from C ``char *`` so the C
  // library's view of string arguments is consistent with its
  // usual conventions. See the 'str marshalling' comments below.
  auto intrinsic_it = c_intrinsic_map.find(sym->name);
  if(intrinsic_it != c_intrinsic_map.end())
  {
    const std::string &c_name = intrinsic_it->second;

    // Parse-time constant folding. If a ``fold=`` keyword was
    // set on the decorator *and* every argument at this call
    // site is a float (or int-that-converts-to-float) constant,
    // evaluate the named host-side op (from <cmath>) and return
    // the result as a constant expression. Falls through to the
    // C-call path for non-constant arguments or unrecognised
    // fold names.
    //
    // If ``domain=`` is also set, a constant argument that fails
    // the named domain predicate raises Python ValueError
    // (matching CPython's math-domain semantics). A nondet
    // argument leaves the ad-hoc math path (in
    // imported_math_funcs) to emit the guarded ValueError and
    // the nondet+constraints return. This duplication will be
    // retired in a follow-up once the decorator supports the
    // nondet+constraints case too.
    auto fold_it = c_intrinsic_fold_map.find(sym->name);
    auto domain_it = c_intrinsic_domain_map.find(sym->name);
    if(fold_it != c_intrinsic_fold_map.end() && arguments.size() == 1)
    {
      auto cv = try_eval_double(arguments[0]);
      if(cv.has_value())
      {
        double x = cv.value();

        // Domain check for constant args. If out of domain, raise
        // ValueError (matching CPython) and return a nondet
        // sentinel; the exception handler intercepts before the
        // caller observes the value.
        if(domain_it != c_intrinsic_domain_map.end())
        {
          const std::string &dom = domain_it->second;
          bool in_domain = true;
          if(dom == "nonneg")
            in_domain = x >= 0;
          else if(dom == "positive")
            in_domain = x > 0;
          else if(dom == "gt_neg_one")
            in_domain = x > -1;
          else if(dom == "abs_le_1")
            in_domain = x >= -1 && x <= 1;
          else if(dom == "abs_lt_1")
            in_domain = x > -1 && x < 1;
          else if(dom == "ge_1")
            in_domain = x >= 1;
          if(!in_domain)
          {
            emit_value_error(false_exprt{});
            return side_effect_expr_nondett{double_type(), get_location(expr)};
          }
        }

        const std::string &op = fold_it->second;
        double r = 0;
        bool computed = true;
        if(op == "sqrt")
          r = std::sqrt(x);
        else if(op == "cbrt")
          r = std::cbrt(x);
        else if(op == "exp")
          r = std::exp(x);
        else if(op == "exp2")
          r = std::exp2(x);
        else if(op == "expm1")
          r = std::expm1(x);
        else if(op == "log")
          r = std::log(x);
        else if(op == "log2")
          r = std::log2(x);
        else if(op == "log10")
          r = std::log10(x);
        else if(op == "log1p")
          r = std::log1p(x);
        else if(op == "sin")
          r = std::sin(x);
        else if(op == "cos")
          r = std::cos(x);
        else if(op == "tan")
          r = std::tan(x);
        else if(op == "asin")
          r = std::asin(x);
        else if(op == "acos")
          r = std::acos(x);
        else if(op == "atan")
          r = std::atan(x);
        else if(op == "sinh")
          r = std::sinh(x);
        else if(op == "cosh")
          r = std::cosh(x);
        else if(op == "tanh")
          r = std::tanh(x);
        else if(op == "asinh")
          r = std::asinh(x);
        else if(op == "acosh")
          r = std::acosh(x);
        else if(op == "atanh")
          r = std::atanh(x);
        else if(op == "ceil")
          r = std::ceil(x);
        else if(op == "floor")
          r = std::floor(x);
        else if(op == "trunc")
          r = std::trunc(x);
        else if(op == "fabs")
          r = std::fabs(x);
        else if(op == "erf")
          r = std::erf(x);
        else if(op == "erfc")
          r = std::erfc(x);
        else if(op == "gamma" || op == "tgamma")
          r = std::tgamma(x);
        else if(op == "lgamma")
          r = std::lgamma(x);
        else if(op == "ulp")
        {
          // PLR / IEEE-754: math.ulp(x) returns the unit in
          // the last place at x (i.e. spacing to the next
          // representable double). Equivalent to
          // nextafter(|x|, +inf) - |x|.
          double ax = std::fabs(x);
          if(std::isnan(ax) || std::isinf(ax))
            r = ax;
          else if(ax == 0.0)
            r = std::numeric_limits<double>::denorm_min();
          else
          {
            double na =
              std::nextafter(ax, std::numeric_limits<double>::infinity());
            r = na - ax;
          }
        }
        else if(op == "degrees")
          r = x * 180.0 / M_PI;
        else if(op == "radians")
          r = x * M_PI / 180.0;
        else
          computed = false;
        if(computed && std::isfinite(r))
          return double_to_floatbv(r);
      }
    }

    // Two-arg fold: math.pow(x, y), atan2(y, x), hypot(x, y),
    // fmod(x, y), copysign(x, y), remainder(x, y). All args
    // must be float/int constants; when both are, evaluate
    // via std::<op>.
    if(fold_it != c_intrinsic_fold_map.end() && arguments.size() == 2)
    {
      auto cv1 = try_eval_double(arguments[0]);
      auto cv2 = try_eval_double(arguments[1]);
      if(cv1.has_value() && cv2.has_value())
      {
        const std::string &op = fold_it->second;
        double a = cv1.value(), b = cv2.value();
        double r = 0;
        bool computed = true;
        if(op == "pow")
          r = std::pow(a, b);
        else if(op == "atan2")
          r = std::atan2(a, b);
        else if(op == "hypot")
          r = std::hypot(a, b);
        else if(op == "fmod")
          r = std::fmod(a, b);
        else if(op == "copysign")
          r = std::copysign(a, b);
        else if(op == "remainder")
          r = std::remainder(a, b);
        else if(op == "ldexp")
        {
          // PLR: math.ldexp(x, i) = x * 2**i. Second arg is int,
          // try_eval_double already converts it to double.
          r = std::ldexp(a, static_cast<int>(b));
        }
        else if(op == "nextafter")
        {
          r = std::nextafter(a, b);
        }
        else
          computed = false;
        if(computed && std::isfinite(r))
          return double_to_floatbv(r);
      }
    }

    // Symbolic-argument handling for decorator-driven math.
    //
    // When fold= is set but the argument is symbolic (not a
    // compile-time constant), we model the call as a nondet
    // return constrained by the optional domain= and range=
    // keywords — matching the semantics of CPython's math
    // functions plus CBMC's C math-library model:
    //
    //   * domain= named predicate: emit guarded ValueError if
    //     the predicate rejects the argument at runtime.
    //   * range= named predicate: constrain the nondet return
    //     accordingly.
    //
    // Return type follows func_type (the Python declaration).
    // For most math functions this is double_type().
    auto range_it = c_intrinsic_range_map.find(sym->name);
    bool is_math_float_fn =
      fold_it != c_intrinsic_fold_map.end() && arguments.size() == 1 &&
      to_code_type(func_type).return_type().id() == ID_floatbv;
    if(is_math_float_fn)
    {
      std::string dom = domain_it != c_intrinsic_domain_map.end()
                          ? domain_it->second
                          : std::string{};
      std::string rng = range_it != c_intrinsic_range_map.end()
                          ? range_it->second
                          : std::string{};
      return emit_math_intrinsic_nondet(
        dom, rng, arguments[0], get_location(expr));
    }

    const pointer_typet c_char_ptr{char_type(), config.ansi_c.pointer_width};

    // Helper: is this type a Python refined string type?
    auto is_py_str = [](const typet &t) { return is_python_string_type(t); };

    // Build the C function signature by projecting each Python
    // parameter onto a C equivalent. Python str → C char*.
    // Python int → C signed int of the width declared by the
    // @c_intrinsic('name', int_width=N) annotation (default 64).
    auto iw_it = c_intrinsic_int_width_map.find(sym->name);
    int int_width =
      iw_it != c_intrinsic_int_width_map.end() ? iw_it->second : 64;
    auto maybe_narrow_int = [&](typet &t)
    {
      if(t.id() == ID_signedbv)
      {
        const auto sz = to_signedbv_type(t).get_width();
        if(
          static_cast<int>(sz) != int_width &&
          (int_width == 32 || int_width == 64))
          t = signedbv_typet{static_cast<std::size_t>(int_width)};
      }
    };
    code_typet c_func_type = to_code_type(func_type);
    for(auto &p : c_func_type.parameters())
    {
      if(is_py_str(p.type()))
        p.type() = c_char_ptr;
      else
        maybe_narrow_int(p.type());
      p.set_identifier(irep_idt{});
    }
    typet c_return_type = c_func_type.return_type();
    const bool return_is_py_str = is_py_str(c_return_type);
    if(return_is_py_str)
      c_func_type.return_type() = c_char_ptr;
    else
      maybe_narrow_int(c_func_type.return_type());

    irep_idt c_id{c_name};
    if(symbol_table.lookup(c_id) == nullptr)
    {
      symbolt c_sym{c_id, c_func_type, ID_C};
      c_sym.base_name = c_name;
      c_sym.location = get_location(expr);
      c_sym.is_lvalue = true;
      c_sym.is_extern = true;
      symbol_table.add(c_sym);
    }

    // Marshal arguments: for each parameter typed as Python str,
    // extract the struct's 'data' pointer and hand that to C.
    // For string-literal arguments (produced by
    // build_string_struct), the backing array is a *temporary*
    // and taking its address produces a pointer CBMC treats as a
    // "dead object" when dereferenced later. build_string_literal
    // wraps the same bytes into a static-lifetime symbol and
    // returns a new struct with the safe pointer; we detect the
    // inline literal shape and substitute, then extract the raw
    // pointer without going through a struct temporary (that
    // temporary loses track of the persistent storage).
    const auto &c_params = c_func_type.parameters();
    for(std::size_t i = 0; i < arguments.size() && i < c_params.size(); i++)
    {
      const typet &py_param_type =
        to_code_type(func_type).parameters()[i].type();
      if(is_py_str(py_param_type))
      {
        // Try to recognise the build_string_struct shape:
        //   { length-constant, address_of(array-literal[0]) }
        // If matched, hand C the address_of directly — that's a
        // genuine pointer into persistent storage, not a
        // member-access into a temporary struct.
        if(
          arguments[i].id() == ID_struct && arguments[i].operands().size() == 2)
        {
          const exprt &len_op = arguments[i].operands()[0];
          const exprt &data_op = arguments[i].operands()[1];
          if(
            len_op.is_constant() && data_op.id() == ID_address_of &&
            to_address_of_expr(data_op).object().id() == ID_index)
          {
            const exprt &arr =
              to_index_expr(to_address_of_expr(data_op).object()).array();
            if(arr.id() == ID_array)
            {
              std::string bytes;
              bytes.reserve(arr.operands().size());
              bool all_bytes = true;
              for(const auto &op : arr.operands())
              {
                if(!op.is_constant())
                {
                  all_bytes = false;
                  break;
                }
                mp_integer v;
                if(to_integer(to_constant_expr(op), v))
                {
                  all_bytes = false;
                  break;
                }
                bytes.push_back(static_cast<char>(v.to_long()));
              }
              // (build_string_struct doesn't append a trailing
              // NUL — string_constantt below handles that.)
              if(all_bytes)
              {
                // Emit a C string_constantt — CBMC recognises
                // these as persistent and exempt from dead-object
                // checks, exactly like C code's "literal"
                // constructs. Pass address_of(str[0]) as the
                // C callee's char*.
                string_constantt sc{irep_idt{bytes}};
                arguments[i] = typecast_exprt{
                  address_of_exprt{index_exprt{
                    sc, from_integer(0, signedbv_typet{64}), char_type()}},
                  c_char_ptr};
                continue;
              }
            }
          }
        }

        exprt data = member_exprt{
          arguments[i],
          "data",
          pointer_typet{unsignedbv_typet{8}, config.ansi_c.pointer_width}};
        arguments[i] = typecast_exprt{std::move(data), c_char_ptr};
      }
      else if(
        arguments[i].type().id() == ID_signedbv &&
        c_params[i].type().id() == ID_signedbv &&
        arguments[i].type() != c_params[i].type())
      {
        // int width narrowing / widening for int_width= callees.
        arguments[i] = typecast_exprt{arguments[i], c_params[i].type()};
      }
    }

    symbol_exprt callee_expr = symbol_table.lookup_ref(c_id).symbol_expr();
    callee_expr.add_source_location() = get_location(expr);
    exprt call = side_effect_expr_function_callt{
      std::move(callee_expr),
      std::move(arguments),
      c_func_type.return_type(),
      get_location(expr)};

    if(!return_is_py_str)
    {
      // If int_width= narrowed the return type, widen back to
      // the Python-declared int so downstream code gets the
      // expected signedbv width.
      const typet &py_return = to_code_type(func_type).return_type();
      if(
        call.type().id() == ID_signedbv && py_return.id() == ID_signedbv &&
        call.type() != py_return)
      {
        return typecast_exprt{std::move(call), py_return};
      }
      return call;
    }

    // Marshal the return: wrap the returned char* into a Python
    // refined-string struct. The length is nondet (we can't
    // compute strlen precisely without a separate intrinsic), but
    // for a sound verification over-approximation we constrain it
    // to be in [0, PYTHON_MAX_STRING_LENGTH].
    // We store the call's result in a fresh temp first because
    // we want to read it multiple times (for the data and the
    // fake length) without duplicating side effects.
    static unsigned cstr_ret_ctr = 0;
    std::string rn = "__cstr_ret_" + std::to_string(cstr_ret_ctr++);
    std::string rq = qualify_name(rn);
    irep_idt rid{rq};
    if(symbol_table.lookup(rid) == nullptr)
    {
      symbolt rs{rid, c_char_ptr, "python"};
      rs.base_name = rn;
      rs.is_lvalue = true;
      rs.is_state_var = true;
      symbol_table.add(rs);
    }
    symbol_exprt ret_ptr = symbol_table.lookup_ref(rid).symbol_expr();
    pending_checks.push_back(code_frontend_assignt{ret_ptr, call});

    // Nondet length, constrained to [0, PYTHON_MAX_STRING_LENGTH].
    std::string ln = "__cstr_len_" + std::to_string(cstr_ret_ctr - 1);
    std::string lq = qualify_name(ln);
    irep_idt lid{lq};
    if(symbol_table.lookup(lid) == nullptr)
    {
      symbolt ls{lid, signedbv_typet{64}, "python"};
      ls.base_name = ln;
      ls.is_lvalue = true;
      ls.is_state_var = true;
      symbol_table.add(ls);
    }
    symbol_exprt ret_len = symbol_table.lookup_ref(lid).symbol_expr();
    pending_checks.push_back(code_frontend_assignt{
      ret_len,
      side_effect_expr_nondett{signedbv_typet{64}, source_locationt{}}});
    pending_checks.push_back(code_assumet{binary_relation_exprt{
      ret_len, ID_ge, from_integer(0, signedbv_typet{64})}});
    pending_checks.push_back(code_assumet{binary_relation_exprt{
      ret_len,
      ID_le,
      from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64})}});

    // Build the Python refined-string struct {length, data} where
    // data is the C pointer we just saved.
    return struct_exprt{
      {ret_len,
       typecast_exprt{
         ret_ptr,
         pointer_typet{unsignedbv_typet{8}, config.ansi_c.pointer_width}}},
      python_string_type()};
  }

  side_effect_expr_function_callt call{
    sym->symbol_expr(),
    std::move(arguments),
    func_type.return_type(),
    get_location(expr)};

  return std::move(call);
}
