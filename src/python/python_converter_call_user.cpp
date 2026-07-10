/// Python to GOTO converter — user-function-call resolution
/// (PLR §4.2.1, §6.3.4). Handles nested function definitions,
/// lambdas, decorator wrappers, callable-instance __call__
/// dispatch, the @c_intrinsic redirection that delegates to
/// the corresponding C function, function aliases registered
/// via `register_function_alias`, and the keyword/vararg
/// argument binding that produces the final
/// side_effect_expr_function_callt. Extracted from
/// python_converter_call.cpp per
/// doc/python-frontend-architecture.md.
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

// §12 higher-order monomorphisation. See the declaration in
// python_converter.h for the contract.
bool python_convertert::try_monomorphise_call(
  const std::string &func_name,
  const jsont &args,
  const symbolt &hof_sym,
  const irep_idt &hof_id,
  irep_idt &clone_id,
  std::set<std::size_t> &callable_positions)
{
  if(!args.is_array() || hof_sym.type.id() != ID_code)
    return false;
  // Only specialise a defined body; a placeholder / nil value has
  // nothing to re-convert.
  if(hof_sym.value.is_nil() || hof_sym.value.id() != ID_code)
    return false;

  // Copy everything we need from hof_sym BY VALUE up front: the calls
  // below (convert_lambda, symbol_table.add, convert_statement) mutate
  // the symbol table and may invalidate references into it.
  const code_typet hof_type = to_code_type(hof_sym.type);
  const source_locationt hof_loc = hof_sym.location;
  const auto &hparams = hof_type.parameters();
  // Methods (first param `self`) dispatch differently; free
  // functions only here.
  if(!hparams.empty() && id2string(hparams[0].get_base_name()) == "self")
    return false;

  // Locate the function's FunctionDef AST (top level, or nested one
  // level inside another function/class). We need its body to
  // re-convert specialised.
  std::function<const jsont *(const jsont &, const std::string &)> find_fn =
    [&](const jsont &scope_body, const std::string &name) -> const jsont *
  {
    if(!scope_body.is_array())
      return nullptr;
    for(const auto &s : as_array(scope_body))
    {
      if(
        is_node_type(s, "FunctionDef") &&
        json_string(json_member(s, "name")) == name)
        return &s;
      if(is_node_type(s, "FunctionDef") || is_node_type(s, "ClassDef"))
      {
        if(const jsont *r = find_fn(json_member(s, "body"), name))
          return r;
      }
    }
    return nullptr;
  };
  const jsont *fn_ast =
    find_fn(json_member(parse_tree.ast_json, "body"), func_name);
  if(fn_ast == nullptr)
    return false;
  // A decorated HOF is already handled by the decorator path.
  {
    const jsont &decs = json_member(*fn_ast, "decorator_list");
    if(decs.is_array() && !as_array(decs).empty())
      return false;
  }

  // Does the body call the parameter named `pname` (i.e. `pname(...)`)?
  std::function<bool(const jsont &, const std::string &)> body_calls =
    [&](const jsont &n, const std::string &pname) -> bool
  {
    if(n.is_array())
    {
      for(const auto &e : as_array(n))
        if(body_calls(e, pname))
          return true;
      return false;
    }
    if(!n.is_object())
      return false;
    if(is_node_type(n, "Call"))
    {
      const jsont &f = json_member(n, "func");
      if(is_node_type(f, "Name") && json_string(json_member(f, "id")) == pname)
        return true;
    }
    static const std::vector<std::string> fields{
      "body",  "orelse",  "finalbody",   "handlers", "elt",        "key",
      "value", "values",  "elts",        "keys",     "generators", "iter",
      "ifs",   "test",    "comparators", "left",     "right",      "operand",
      "func",  "args",    "keywords",    "slice",    "targets",    "target",
      "items", "operands"};
    for(const auto &fld : fields)
    {
      const jsont &c = json_member(n, fld);
      if(!c.is_null() && body_calls(c, pname))
        return true;
    }
    return false;
  };

  // Resolve a positional argument AST to a callable symbol id, or
  // empty if it isn't a (resolvable) callable.
  auto resolve_callable = [&](const jsont &arg) -> irep_idt
  {
    if(is_node_type(arg, "Lambda"))
    {
      exprt l = convert_lambda(arg);
      if(l.id() == ID_symbol && l.type().id() == ID_code)
        return to_symbol_expr(l).get_identifier();
      return irep_idt{};
    }
    if(is_node_type(arg, "Name"))
    {
      std::string nm = json_string(json_member(arg, "id"));
      auto ai = function_aliases.find(qualify_name(nm));
      if(ai != function_aliases.end())
        return ai->second;
      irep_idt scoped{"python::" + current_function + "::" + nm};
      const symbolt *ss = symbol_table.lookup(scoped);
      if(ss != nullptr && ss->type.id() == ID_code)
        return scoped;
      irep_idt bare{"python::" + nm};
      const symbolt *bs = symbol_table.lookup(bare);
      if(bs != nullptr && bs->type.id() == ID_code)
        return bare;
    }
    return irep_idt{};
  };

  // Gather bindings: positional arg i → callable, when param i is
  // actually called in the body.
  struct bindingt
  {
    std::size_t pos;
    std::string pname;
    irep_idt callable;
  };
  std::vector<bindingt> bindings;
  const auto &arg_array = as_array(args);
  std::size_t i = 0;
  for(const auto &arg : arg_array)
  {
    if(is_node_type(arg, "Starred"))
      return false; // keep it simple/sound
    if(i < hparams.size())
    {
      const std::string pname = id2string(hparams[i].get_base_name());
      if(body_calls(*fn_ast, pname))
      {
        irep_idt cid = resolve_callable(arg);
        if(!cid.empty())
          bindings.push_back({i, pname, cid});
      }
    }
    ++i;
  }

  // Also bind parameters whose DEFAULT value is a resolvable callable and
  // which are called in the body, for call sites that rely on the default
  // (e.g. `g = f; def h(op=g): return op(1, 1)` called as `h()`). The
  // default is filled by the normal default-argument path; the clone's
  // function_aliases entry makes `op(...)` resolve to the callable.
  {
    const jsont &fargs = json_member(*fn_ast, "args");
    const jsont &params = json_member(fargs, "args");
    const jsont &defaults = json_member(fargs, "defaults");
    if(params.is_array() && defaults.is_array())
    {
      std::vector<const jsont *> default_nodes;
      for(const auto &d : as_array(defaults))
        default_nodes.push_back(&d);
      const std::size_t nparams = as_array(params).size();
      const std::size_t ndefaults = default_nodes.size();
      const std::size_t first_default =
        nparams >= ndefaults ? nparams - ndefaults : 0;
      for(std::size_t pi = 0; pi < hparams.size() && pi < nparams; ++pi)
      {
        if(pi < arg_array.size())
          continue; // supplied positionally (handled above)
        if(pi < first_default)
          continue; // no default for this parameter
        bool already = false;
        for(const auto &b : bindings)
          if(b.pos == pi)
            already = true;
        if(already)
          continue;
        const std::string pname = id2string(hparams[pi].get_base_name());
        if(!body_calls(*fn_ast, pname))
          continue;
        // PLR §8.7: prefer the callable snapshotted at definition time
        // (so a later reassignment of the default's source variable does
        // not redirect the default); fall back to resolving the AST node.
        irep_idt cid;
        auto si = default_callable_snapshot.find({func_name, pi});
        if(si != default_callable_snapshot.end())
          cid = si->second;
        else
          cid = resolve_callable(*default_nodes[pi - first_default]);
        if(!cid.empty())
          bindings.push_back({pi, pname, cid});
      }
    }
  }

  if(bindings.empty())
    return false;

  for(const auto &b : bindings)
    callable_positions.insert(b.pos);

  // Specialise the clone's parameter types to THIS call site's argument
  // types (like a template instantiation), refining e.g. an unannotated
  // `python_value` parameter to the concrete `list[int]` actually passed.
  // Without this the callee body is converted against the generic type —
  // a comprehension over a `python_value` parameter, for instance, is not
  // recognised as iterating a list and is silently dropped. Convert each
  // non-callable positional argument for its type only (its checks are
  // discarded here; the real call below re-converts it).
  std::vector<typet> refined_types;
  for(const auto &hp : hparams)
    refined_types.push_back(hp.type());
  {
    std::vector<codet> saved_pc;
    saved_pc.swap(pending_checks);
    std::size_t ai = 0;
    for(const auto &arg : arg_array)
    {
      if(ai < hparams.size() && !callable_positions.count(ai))
      {
        exprt a = convert_expression(arg);
        if(a.is_not_nil() && a.type().id() != ID_empty)
          refined_types[ai] = a.type();
      }
      ++ai;
    }
    saved_pc.swap(pending_checks);
  }

  // Cache key: HOF + each (position, callable) + the refined parameter
  // types. Distinct callables (e.g. different lambdas per call site) and
  // distinct argument types get distinct, sound clones; an identical
  // pattern (a loop body) reuses one clone.
  std::string key = id2string(hof_id) + "$mono";
  for(const auto &b : bindings)
    key += "$" + std::to_string(b.pos) + "@" + id2string(b.callable);
  for(const auto &t : refined_types)
    key += "#" + t.id_string();
  if(auto it = monomorph_cache.find(key); it != monomorph_cache.end())
  {
    clone_id = it->second;
    return true;
  }

  // Build a freshly-scoped clone.
  const std::string clone_scope =
    func_name + "$mono_" + std::to_string(monomorph_counter++);
  clone_id = irep_idt{"python::" + clone_scope};

  code_typet::parameterst cparams;
  std::size_t pidx = 0;
  for(const auto &hp : hparams)
  {
    const std::string pbase = id2string(hp.get_base_name());
    const typet ptype = refined_types[pidx++];
    irep_idt pid{"python::" + clone_scope + "::" + pbase};
    if(symbol_table.lookup(pid) == nullptr)
    {
      symbolt ps{pid, ptype, "python"};
      ps.base_name = pbase;
      ps.is_lvalue = true;
      ps.is_state_var = true;
      ps.is_parameter = true;
      symbol_table.add(ps);
    }
    code_typet::parametert p{ptype};
    p.set_identifier(pid);
    p.set_base_name(pbase);
    cparams.push_back(p);
  }

  // Register the clone symbol (placeholder body) before conversion so
  // a (self-)recursive HOF resolves to the clone.
  symbolt clone_sym{
    clone_id, code_typet{cparams, hof_type.return_type()}, "python"};
  clone_sym.base_name = clone_scope;
  clone_sym.location = hof_loc;
  clone_sym.is_lvalue = true;
  clone_sym.value = code_blockt{};
  symbol_table.add(clone_sym);
  monomorph_cache[key] = clone_id;

  // Bind the callable parameters in the clone scope; convert the body.
  std::vector<std::string> alias_keys;
  for(const auto &b : bindings)
  {
    std::string akey = "python::" + clone_scope + "::" + b.pname;
    function_aliases[akey] = b.callable;
    alias_keys.push_back(akey);
  }
  const std::string saved_fn = current_function;
  std::vector<std::string> saved_encl;
  saved_encl.swap(enclosing_functions);
  current_function = clone_scope;
  std::vector<codet> saved_pending;
  saved_pending.swap(pending_checks);

  code_blockt new_body;
  const jsont &body_ast = json_member(*fn_ast, "body");
  // Re-run the empty-list element-type prescan in the clone's scope so
  // that `result = []; result.append(...)` inside the specialised body
  // infers its element type against the call-site-specialised parameter
  // types (the concrete list the HOF was actually given), rather than
  // the HOF's generic def-time parameter types.
  collect_empty_list_inferred_types(body_ast);
  if(body_ast.is_array())
    for(const auto &st : as_array(body_ast))
      new_body.add(convert_statement(st));
  // Infer the clone's return type from its *specialised* body: with the
  // callable resolved, `return [x for x in xs if cond(x)]` now yields a
  // proper list, whereas the HOF's def-time return type was inferred
  // while the parameter call was an unresolved nondet (which made e.g.
  // `len(result)` raise a spurious TypeError). The return may be nested
  // inside a block (convert_statement prepends the comprehension loop to
  // it), so search recursively; prefer the first explicit return's value
  // type and fall back to the HOF's declared return type.
  typet clone_ret = hof_type.return_type();
  {
    std::function<const exprt *(const codet &)> find_ret =
      [&](const codet &c) -> const exprt *
    {
      if(c.get_statement() == ID_return)
      {
        const auto &ret = static_cast<const code_frontend_returnt &>(c);
        if(ret.has_return_value() && ret.return_value().type().id() != ID_empty)
          return &ret.return_value();
        return nullptr;
      }
      for(const auto &op : c.operands())
        if(op.id() == ID_code)
          if(const exprt *r = find_ret(static_cast<const codet &>(op)))
            return r;
      return nullptr;
    };
    if(const exprt *r = find_ret(new_body))
      clone_ret = r->type();
  }
  if(clone_ret.id() != ID_empty)
    new_body.add(code_frontend_returnt{safe_zero(clone_ret)});

  saved_pending.swap(pending_checks);
  current_function = saved_fn;
  saved_encl.swap(enclosing_functions);
  for(const auto &akey : alias_keys)
    function_aliases.erase(akey);

  symbolt &clone_writeable = symbol_table.get_writeable_ref(clone_id);
  clone_writeable.type = code_typet{cparams, clone_ret};
  clone_writeable.value = new_body;
  return true;
}

exprt python_convertert::convert_user_call(
  const jsont &expr,
  const std::string &func_name,
  const jsont &args)
{
  // PLR §6.2.9 generator identity: a generator is a REFERENCE; passing it to a
  // function that consumes it advances the SAME cursor, visible to the caller.
  // The list-with-cursor model uses a per-Name cursor and passes the backing
  // list BY VALUE, so a callee's consumption is NOT reflected in the caller's
  // cursor -- `it=g(); c(it); next(it)` re-yielded from the start (a false
  // proof). A precise shared cursor doesn't fit the once-converted-callee model,
  // so SOUNDLY over-approximate: after passing a generator Name to a user
  // function, its cursor may have advanced by an unknown amount -> havoc it to a
  // nondet position in [0, length]. Subsequent next()/for/list then read an
  // unknown position (no false proof); a callee that doesn't consume loses
  // precision (sound). Emitted via pending_checks (flushed at the statement);
  // the call itself does not read the caller's cursor, so ordering is immaterial.
  if(args.is_array())
  {
    for(const auto &a : as_array(args))
    {
      if(!is_node_type(a, "Name"))
        continue;
      irep_idt aid{qualify_name(json_string(json_member(a, "id")))};
      auto gc = generator_cursors.find(aid);
      if(gc == generator_cursors.end())
        continue;
      const symbolt *cur = symbol_table.lookup(gc->second);
      const symbolt *lst = symbol_table.lookup(aid);
      if(cur == nullptr || lst == nullptr || !is_python_list_type(lst->type))
        continue;
      const signedbv_typet i64{64};
      symbol_exprt cur_e = cur->symbol_expr();
      pending_checks.push_back(code_frontend_assignt{
        cur_e, side_effect_expr_nondett{i64, get_location(expr)}});
      member_exprt len{lst->symbol_expr(), "length", i64};
      pending_checks.push_back(code_assumet{and_exprt{
        binary_relation_exprt{cur_e, ID_ge, from_integer(0, i64)},
        binary_relation_exprt{cur_e, ID_le, len}}});
    }
  }
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

  // §12 higher-order monomorphisation: if a callable argument is
  // passed to a user function that calls the matching parameter,
  // redirect this call to a specialised clone where that parameter is
  // bound to the callable (so `fn(...)` inside resolves precisely
  // instead of becoming a nondet "no body for callee"). The bound
  // callable arguments are then redundant and passed as nondet
  // placeholders.
  std::set<std::size_t> monomorph_nondet_positions;
  {
    const irep_idt resolved_id = sym->name;
    irep_idt clone_id;
    std::set<std::size_t> cps;
    if(try_monomorphise_call(func_name, args, *sym, symbol_id, clone_id, cps))
    {
      symbol_id = clone_id;
      monomorph_nondet_positions = std::move(cps);
      sym = symbol_table.lookup(clone_id);
    }
    else
    {
      // Refresh from the already-resolved id: try_monomorphise_call may
      // have added symbols (invalidating the previous pointer), but the
      // callee resolution above (incl. alias redirects) still holds.
      sym = symbol_table.lookup(resolved_id);
    }
    if(sym == nullptr || sym->type.id() != ID_code)
      return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }

  const code_typet &func_type = to_code_type(sym->type);
  const auto &params = func_type.parameters();

  // Root B (import scoping, PLR §4.2): a bare call to a name that
  // exists in the flat python:: table ONLY because a module was
  // imported (it is in imported_module_defs) but was NOT itself
  // imported, and is not a main-module definition, is a NameError in
  // CPython (`from X import a; b()` where b is defined in X).
  //
  // Restricted to code SYNTACTICALLY in the main module: module level
  // (current_function empty) or inside a function defined in main
  // (current_function in main_module_defs). Imported-module function
  // bodies legitimately call their own module's (un-imported-by-main)
  // names, and are converted lazily so !processing_import does not
  // exclude them -- the current_function gate does. Disabled if a
  // `from X import *` was seen (names not enumerable).
  if(
    !saw_import_star &&
    (current_function.empty() ||
     main_module_defs.count(current_function) > 0))
  {
    const std::string bn = id2string(sym->base_name);
    if(
      imported_module_defs.count(bn) > 0 &&
      explicitly_imported_names.count(bn) == 0 &&
      main_module_defs.count(bn) == 0)
    {
      emit_conditional_exception(true_exprt{}, "NameError");
      return side_effect_expr_nondett{python_int_type(), get_location(expr)};
    }
  }

  // @may_raise('ExcType'): under --python-raising-ops-check, model the
  // decorated stub (e.g. os.remove -> OSError) as may-raise so the
  // uncaught-exception / except path is explored. No-op otherwise.
  if(python_raising_ops_check)
  {
    auto mr = may_raise_map.find(sym->name);
    if(mr != may_raise_map.end())
      emit_may_raise(mr->second.c_str());
  }

  // Build argument list: start with positional args
  exprt::operandst arguments;
  // Set when `f(*xs)` forwards a whole list directly into the target's *args
  // (vararg) parameter -- the packing step below then skips re-packing.
  bool vararg_bound_directly = false;
  if(args.is_array())
  {
    std::size_t arg_index = 0;
    for(const auto &arg : as_array(args))
    {
      const std::size_t cur_idx = arg_index++;
      // Monomorphised callable argument: bound into the clone via
      // function_aliases, its value is unused — pass a nondet
      // placeholder of the parameter type.
      if(monomorph_nondet_positions.count(cur_idx))
      {
        const typet pt = cur_idx < params.size() ? params[cur_idx].type()
                                                 : python_value_type();
        arguments.push_back(side_effect_expr_nondett{pt, get_location(expr)});
        continue;
      }
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
        // PEP 448 / PLR §6.3.4: f(*t) where t is a TUPLE. A tuple struct's
        // operands ARE its elements (no length/data split like a list), so
        // spread each element as a positional argument. Covers a tuple literal
        // (struct) and a Name bound to a tuple literal (tuple_literals). Handled
        // before the list-layout path below (which would misread element 0 as a
        // length field).
        if(is_python_tuple_type(inner.type()))
        {
          const exprt *tlit = nullptr;
          if(inner.id() == ID_struct)
            tlit = &inner;
          else if(inner.id() == ID_symbol)
          {
            auto tit =
              tuple_literals.find(to_symbol_expr(inner).get_identifier());
            if(tit != tuple_literals.end())
              tlit = &tit->second;
          }
          if(tlit != nullptr && tlit->id() == ID_struct)
          {
            for(const auto &el : tlit->operands())
              arguments.push_back(el);
            continue;
          }
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
          // PLR §8.7: `f(*xs)` forwarding a NON-literal list into the target's
          // *args (vararg) parameter. The vararg absorbs a SYMBOLIC number of
          // elements (xs's runtime length), which the fixed-count spread below
          // cannot represent -- it read `params.size()-already` elements (one
          // slot for the vararg) and so computed the wrong count (`h(*xs)` gave
          // sum(xs[:1]) not sum(xs), a false proof found by the mutation-
          // oracle). When this `*xs` lands exactly at the vararg slot and is the
          // LAST positional arg, bind the vararg to a SOUND nondet list. (A
          // precise same-length forwarding is future work: the untyped vararg
          // param would force an expensive element-wise python_value re-wrap.)
          {
            auto va_it = function_vararg_index.find(sym->name);
            if(
              va_it != function_vararg_index.end() &&
              arguments.size() == va_it->second &&
              cur_idx + 1 == as_array(args).size() &&
              va_it->second < params.size() &&
              is_python_list_type(params[va_it->second].type()))
            {
              arguments.push_back(side_effect_expr_nondett{
                params[va_it->second].type(), get_location(expr)});
              vararg_bound_directly = true;
              continue;
            }
          }
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
      // Fat-closure boxing at the param boundary
      // (doc/python-frontend-fat-closure-plan.md): a capturing closure
      // passed as a value argument — here a factory CALL returning a
      // closure, `apply(make())` — is boxed into a CLOSURE python_value
      // carrying a per-instance snapshot of its captures. The factory
      // body is run so the captures are computed, then snapshotted; the
      // callee dispatches the CLOSURE value soundly across calls.
      if(is_node_type(arg, "Call"))
      {
        const jsont &cf = json_member(arg, "func");
        if(is_node_type(cf, "Name"))
        {
          std::string cn = json_string(json_member(cf, "id"));
          auto lr = lambda_returning_functions.find(cn);
          auto ci = (lr != lambda_returning_functions.end())
                      ? closure_captures.find(id2string(lr->second))
                      : closure_captures.end();
          const symbolt *fsym =
            (lr != lambda_returning_functions.end())
              ? symbol_table.lookup(irep_idt{"python::" + cn})
              : nullptr;
          if(
            ci != closure_captures.end() && !ci->second.empty() &&
            fsym != nullptr && fsym->type.id() == ID_code)
          {
            const auto &fp = to_code_type(fsym->type).parameters();
            // Map factory parameter id -> the call's converted argument.
            std::map<std::string, exprt> param_arg;
            const jsont &fa = json_member(arg, "args");
            if(fa.is_array())
            {
              auto ai = as_array(fa).begin();
              for(std::size_t i = 0; i < fp.size() && ai != as_array(fa).end();
                  ++i, ++ai)
                param_arg[id2string(fp[i].get_identifier())] =
                  convert_expression(*ai);
            }
            // A capture whose source is NOT a factory parameter is a
            // factory LOCAL; computing it requires running the factory
            // body. Param captures are taken from the args directly
            // (reading the param symbol after the call is unreliable
            // across repeated calls to the same factory).
            bool has_local = false;
            for(const auto &cap : ci->second)
              if(!param_arg.count(std::get<0>(cap)))
                has_local = true;
            if(has_local)
            {
              exprt::operandst fargs;
              if(fa.is_array())
                for(const auto &a2 : as_array(fa))
                  fargs.push_back(convert_expression(a2));
              side_effect_expr_function_callt fcall{
                fsym->symbol_expr(),
                std::move(fargs),
                to_code_type(fsym->type).return_type(),
                get_location(arg)};
              pending_checks.push_back(code_expressiont{std::move(fcall)});
            }
            exprt::operandst caps;
            static unsigned capsnap = 0;
            for(const auto &cap : ci->second)
            {
              auto pa = param_arg.find(std::get<0>(cap));
              if(pa != param_arg.end())
              {
                caps.push_back(pa->second); // param capture -> the arg
                continue;
              }
              const symbolt *os =
                symbol_table.lookup(irep_idt{std::get<0>(cap)});
              if(os == nullptr)
              {
                caps.push_back(side_effect_expr_nondett{
                  std::get<2>(cap), get_location(arg)});
                continue;
              }
              // Local capture: snapshot after the factory ran.
              std::string sn = "__capsnap_" + std::to_string(capsnap++);
              irep_idt sid{qualify_name(sn)};
              if(symbol_table.lookup(sid) == nullptr)
              {
                symbolt ss{sid, os->type, "python"};
                ss.base_name = sn;
                ss.is_lvalue = true;
                ss.is_state_var = true;
                ss.is_static_lifetime = current_function.empty();
                symbol_table.add(ss);
              }
              symbol_exprt snap = symbol_table.lookup_ref(sid).symbol_expr();
              pending_checks.push_back(
                code_frontend_assignt{snap, os->symbol_expr()});
              caps.push_back(snap);
            }
            std::vector<codet> box_stmts;
            exprt closure_val =
              box_closure(lr->second, caps, box_stmts, get_location(arg));
            for(auto &s : box_stmts)
              pending_checks.push_back(std::move(s));
            arguments.push_back(closure_val);
            continue;
          }
        }
      }
      arguments.push_back(convert_expression(arg));
    }
  }

  // PLR §8.7: full call-signature validation (too many positional,
  // unknown keyword, missing required positional, multiple values).
  // Routes through the SAME validate_call_signature used for
  // constructors and methods, so all call forms validate uniformly
  // (previously free functions had only a partial inline
  // too-many-positional check). A bound-method value reaches here too
  // (`m = obj.meth; m()`); its receiver supplies `self`, so treat a
  // leading `self` parameter as implicitly provided to avoid a spurious
  // "missing self".
  {
    std::size_t implicit_self =
      (!params.empty() && id2string(params[0].get_base_name()) == "self") ? 1
                                                                          : 0;
    validate_call_signature(sym->name, expr, args, implicit_self);
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
      !vararg_bound_directly && va_it_pre != function_vararg_index.end() &&
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
                  v = coerce_call_argument(
                    v, params[j].type(), params[j].get_identifier());
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
          v = coerce_call_argument(
            v, params[i].type(), params[i].get_identifier());
          arguments[i] = std::move(v);
          matched = true;
          break;
        }
      }
      if(!matched)
        unmatched_kw.push_back({kw_name, kw_val});
    }

    // PLR §8.7: a keyword that matches no parameter raises TypeError
    // ("unexpected keyword argument") unless the function accepts
    // **kwargs. Only for an exact (undecorated) signature.
    if(
      function_signature_checkable.count(sym->name) &&
      !function_has_kwargs.count(sym->name) && !unmatched_kw.empty())
      emit_conditional_exception(true_exprt{}, "TypeError");

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
      // Per-closure-variable binding (PLR §4.2.2): if this call is via a
      // closure variable `g` recorded at `g = factory(...)`, bind each
      // capture from g's own snapshot temp so distinct factory
      // invocations stay independent. Fall back to the original capture
      // source (outer_id) for the non-escaping / in-scope path.
      auto cvc = closure_var_captures.find(qualify_name(func_name));
      for(const auto &[outer_id, name, type] : cap_it2->second)
      {
        irep_idt src{outer_id};
        if(cvc != closure_var_captures.end())
        {
          auto t = cvc->second.find(name);
          if(t != cvc->second.end())
            src = t->second;
        }
        const symbolt *outer_sym = symbol_table.lookup(src);
        if(outer_sym != nullptr)
        {
          // PLR §4.2.2 / §7.5: a captured free variable that was `del`-eted in
          // the enclosing scope is unbound, so reading it -- here, snapshotting
          // it as the closure's capture argument at the call site -- raises
          // NameError. Guard on the enclosing scope's per-name deleted flag
          // (set by `del`, cleared on rebind); a call BEFORE the `del` is fine,
          // one AFTER raises. The flag symbol exists only for del-tracked names,
          // so ordinary closure captures are unaffected. Closes del_in_closure.
          const irep_idt fid{id2string(src) + "$deleted"};
          const symbolt *fsym = symbol_table.lookup(fid);
          if(fsym != nullptr)
            emit_conditional_exception(fsym->symbol_expr(), "NameError");
          arguments.push_back(outer_sym->symbol_expr());
        }
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
        // Invalidate constant-tracking for an lvalue arg so reads after
        // the call don't fold against a pre-call snapshot — the callee
        // may mutate it through the by-reference parameter.
        if(arguments[i].id() == ID_symbol)
        {
          irep_idt sid = to_symbol_expr(arguments[i]).get_identifier();
          list_literals.erase(sid);
          dict_literals.erase(sid);
          tuple_literals.erase(sid);
          string_constants.erase(sid);
          float_constants.erase(sid);
        }
        // safe_typecast's struct→pointer path performs the address-of
        // (materialising rvalues), the list[T]→list[value] element
        // promotion, and the post-call write-back (PLR §3.1) — the same
        // shared boundary the method-dispatch paths use.
        arguments[i] = safe_typecast(arguments[i], params[i].type());
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
          // Provenance gate: only a GENUINELY-annotated parameter is a sound
          // basis for an argument-type mismatch. An inferred/default param
          // type (a lambda param, an unannotated def param specialised from a
          // call site, a `*args`/`**kwargs` element) really accepts Any, so
          // checking it against the arg yields false positives (lambda10,
          // method-signature `*args`, argparse). Same gate as the call-boundary
          // tag obligation.
          explicitly_annotated_params.count(params[i].get_identifier()) &&
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
        // PLR §3.1: a mutable container (dict/list) bound to an Any /
        // python_value parameter is shared by reference -- route it through
        // the same promote+write-back boundary the concrete by-reference path
        // uses (safe_typecast struct->pointer), then wrap the resulting
        // pointer into the tagged union. This makes callee mutations propagate
        // back to the caller's object instead of being lost in wrap_value's
        // throwaway copy (a false proof: a post-call read would fold against
        // the stale value).
        if(
          is_python_value_type(params[i].type()) &&
          arguments[i].id() == ID_symbol &&
          (is_python_dict_type(arguments[i].type()) ||
           is_python_list_type(arguments[i].type())))
        {
          const bool is_dict = is_python_dict_type(arguments[i].type());
          const irep_idt sid = to_symbol_expr(arguments[i]).get_identifier();
          dict_literals.erase(sid);
          list_literals.erase(sid);
          typet canon =
            is_dict
              ? python_dict_type(python_string_type(), python_value_type())
              : python_list_type(python_value_type());
          exprt ptr = safe_typecast(arguments[i], pointer_typet{canon, 64});
          arguments[i] = make_python_value(
            is_dict ? python_type_tagt::DICT : python_type_tagt::LIST, ptr);
        }
        else if(
          is_python_value_type(params[i].type()) &&
          arguments[i].id() == ID_symbol &&
          is_python_set_type(arguments[i].type()))
        {
          // A set is an element-type-agnostic fixed struct (bitmap), so it
          // needs no promotion: share the caller's object directly by address
          // (SET tag) so the callee's mutators propagate without a write-back.
          arguments[i] = make_python_value(
            python_type_tagt::SET, address_of_exprt{arguments[i]});
        }
        else
          arguments[i] = coerce_call_argument(
            arguments[i], params[i].type(), params[i].get_identifier());
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

        if(
          use_smt_string_native &&
          arguments[i].type().id() == ID_smt_string)
        {
          // No char* view of an SMT String; the C intrinsic is a stub, so
          // pass a sound nondet char* (it is not dereferenced by the stub).
          arguments[i] =
            side_effect_expr_nondett{c_char_ptr, get_location(expr)};
        }
        else
        {
          exprt data = member_exprt{
            arguments[i],
            "data",
            pointer_typet{unsignedbv_typet{8}, config.ansi_c.pointer_width}};
          arguments[i] = typecast_exprt{std::move(data), c_char_ptr};
        }
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
    if(use_smt_string_native)
    {
      // The C string content can't be modelled precisely as an SMT String
      // (no strlen reasoning); emit the call for its side effects and return
      // a sound nondet SMT String.
      pending_checks.push_back(code_expressiont{call});
      return bounded_nondet_string(get_location(expr));
    }
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

  // §10: path-sensitive dispatch when this name was bound to more than
  // one callable across branches (`if c: h=f else: h=g; h()`). The
  // conversion-time function_aliases map kept only the last branch's
  // target; the runtime tag (set per branch at the assignment site)
  // selects the correct callee here. Gate strictly on >1 candidate and
  // a fully-matching signature, so all single-target dispatch is
  // unchanged; otherwise fall through to the normal (last-target)
  // build.
  {
    auto cand_it = callable_candidates.find(qualify_name(func_name));
    const symbolt *tag_sym =
      symbol_table.lookup(irep_idt{qualify_name(func_name) + "$callable_tag"});
    if(
      cand_it != callable_candidates.end() && cand_it->second.size() > 1 &&
      tag_sym != nullptr && func_type.return_type().id() != ID_empty)
    {
      const typet &rt = func_type.return_type();
      bool ok = true;
      std::vector<const symbolt *> cand_syms;
      for(const irep_idt &cid : cand_it->second)
      {
        const symbolt *cs = symbol_table.lookup(cid);
        if(cs == nullptr || cs->type.id() != ID_code)
        {
          ok = false;
          break;
        }
        const code_typet &ct = to_code_type(cs->type);
        if(ct.return_type() != rt || ct.parameters().size() != params.size())
        {
          ok = false;
          break;
        }
        for(std::size_t k = 0; k < params.size(); k++)
          if(ct.parameters()[k].type() != params[k].type())
          {
            ok = false;
            break;
          }
        if(!ok)
          break;
        cand_syms.push_back(cs);
      }
      if(ok)
      {
        static unsigned disp_ctr = 0;
        irep_idt rid{
          qualify_name("__call_dispatch_" + std::to_string(disp_ctr++))};
        if(symbol_table.lookup(rid) == nullptr)
        {
          symbolt rs{rid, rt, "python"};
          rs.base_name = id2string(rid).substr(8);
          rs.is_lvalue = true;
          rs.is_state_var = true;
          rs.is_static_lifetime = current_function.empty();
          symbol_table.add(rs);
        }
        symbol_exprt r = symbol_table.lookup_ref(rid).symbol_expr();
        symbol_exprt tag = tag_sym->symbol_expr();
        auto recv_it = bound_method_receivers.find(qualify_name(func_name));
        for(std::size_t i = 0; i < cand_syms.size(); i++)
        {
          // §10: for a bound-method candidate, swap in this branch's
          // receiver (the self argument prepended earlier is the
          // last-processed branch's).
          exprt::operandst args_i = arguments;
          if(
            recv_it != bound_method_receivers.end() &&
            i < recv_it->second.size() && !args_i.empty())
            args_i[0] = recv_it->second[i];
          side_effect_expr_function_callt c{
            cand_syms[i]->symbol_expr(), args_i, rt, get_location(expr)};
          pending_checks.push_back(code_ifthenelset{
            equal_exprt{tag, from_integer(i, tag.type())},
            code_frontend_assignt{r, std::move(c)}});
        }
        return std::move(r);
      }
    }
  }

  side_effect_expr_function_callt call{
    sym->symbol_expr(),
    std::move(arguments),
    func_type.return_type(),
    get_location(expr)};

  return std::move(call);
}
