/// Python to GOTO converter — FunctionDef (PLR §8.7),
/// ClassDef (§9.1), and Expr-as-statement handlers.
///
/// Extracted from python_converter.cpp to reduce the main file size.
/// All logic and class-member state remains unchanged — this file is
/// a pure source-split.

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/config.h>
#include <util/json.h>
#include <util/pointer_expr.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/std_types.h>
#include <util/symbol.h>

#include "python_converter.h"
#include "python_converter_helpers.h"
#include "python_types.h"
#include "python_value_type.h"

#include <set>

// PLR §8.7: Function definitions
// "A function definition defines a user-defined function object."
codet python_convertert::convert_function_def(const jsont &stmt)
{
  // PLR §8.7: skip @overload decorated functions (type hints only)
  const jsont &decorators = json_member(stmt, "decorator_list");
  bool is_c_intrinsic = false;
  std::string c_intrinsic_name;
  std::string c_intrinsic_fold;
  std::string c_intrinsic_domain;
  std::string c_intrinsic_range;
  int c_intrinsic_int_width = 0;
  if(decorators.is_array())
  {
    for(const auto &dec : as_array(decorators))
    {
      if(
        is_node_type(dec, "Name") &&
        json_string(json_member(dec, "id")) == "overload")
        return code_skipt{};
      // @c_intrinsic('NAME', fold='OP', domain='KIND', range='KIND',
      //              int_width=N) — route calls to the named C
      // function. Optional ``fold`` enables parse-time constant
      // folding; optional ``domain`` raises Python ValueError for
      // constant arguments that fail the named domain predicate;
      // optional ``range`` constrains the nondet return for
      // symbolic arguments; optional ``int_width`` overrides the
      // default Python-int→signedbv64 projection for C functions
      // that take/return 32-bit int.
      if(is_node_type(dec, "Call"))
      {
        const jsont &dec_func = json_member(dec, "func");
        if(
          is_node_type(dec_func, "Name") &&
          json_string(json_member(dec_func, "id")) == "c_intrinsic")
        {
          const jsont &dec_args = json_member(dec, "args");
          if(dec_args.is_array() && !as_array(dec_args).empty())
          {
            const jsont &first = *as_array(dec_args).begin();
            if(is_node_type(first, "Constant"))
            {
              c_intrinsic_name = json_string(json_member(first, "value"));
              is_c_intrinsic = !c_intrinsic_name.empty();
            }
          }
          const jsont &dec_kwargs = json_member(dec, "keywords");
          if(dec_kwargs.is_array())
          {
            for(const auto &kw : as_array(dec_kwargs))
            {
              const jsont &val = json_member(kw, "value");
              if(!is_node_type(val, "Constant"))
                continue;
              std::string arg_name = json_string(json_member(kw, "arg"));
              if(arg_name == "int_width")
              {
                // Integer literal: its stringified form lives in
                // .value as a decimal string, not in the json_string()
                // wrapper (which expects a string-typed value).
                const jsont &iv_node = json_member(val, "value");
                try
                {
                  c_intrinsic_int_width = std::stoi(iv_node.value);
                }
                catch(...)
                {
                  c_intrinsic_int_width = 0;
                }
                continue;
              }
              std::string arg_val = json_string(json_member(val, "value"));
              if(arg_name == "fold")
                c_intrinsic_fold = arg_val;
              else if(arg_name == "domain")
                c_intrinsic_domain = arg_val;
              else if(arg_name == "range")
                c_intrinsic_range = arg_val;
            }
          }
        }
      }
    }
  }

  // Phase 2 of the icontract integration plan
  // (doc/python-frontend-icontract-plan.md): collect the
  // @icontract.require / bare @require decorators so we can
  // emit the corresponding precondition assumes at function
  // entry, and (for DFCC) attach __CPROVER_requires clauses to
  // the function type. Each entry is a pointer into the
  // decorator AST owned by `stmt`, so it remains valid for the
  // duration of convert_function_def.
  std::vector<const jsont *> icontract_require_lambdas;
  // Phase 3 (companion): collect @icontract.ensure / bare
  // @ensure decorator lambdas. Lambdas that reference `result`
  // are deferred until Phase 4 wires up the result-binding
  // mechanism — they're collected here but skipped during
  // emission below.
  std::vector<const jsont *> icontract_ensure_lambdas;
  // Phase 5: collect @icontract.snapshot decorators. Each
  // entry pairs a name (the keyword argument `name="..."`) with
  // a pointer to the capture lambda's AST. The capture
  // expression is evaluated at function entry (body
  // prologue), stored in a synthesised per-snapshot symbol,
  // and exposed to ensure lambdas via OLD.<name>.
  std::vector<std::pair<std::string, const jsont *>> icontract_snapshots;
  if(decorators.is_array())
  {
    for(const auto &dec : as_array(decorators))
    {
      if(!is_node_type(dec, "Call"))
        continue;
      const jsont &dec_func = json_member(dec, "func");
      bool is_require = false;
      bool is_ensure = false;
      bool is_snapshot = false;
      // @icontract.require / @icontract.ensure / @icontract.snapshot:
      //   Attribute(Name("icontract"), "require"|"ensure"|"snapshot")
      if(is_node_type(dec_func, "Attribute"))
      {
        std::string attr = json_string(json_member(dec_func, "attr"));
        const jsont &v = json_member(dec_func, "value");
        if(
          is_node_type(v, "Name") &&
          json_string(json_member(v, "id")) == "icontract")
        {
          if(attr == "require")
            is_require = true;
          else if(attr == "ensure")
            is_ensure = true;
          else if(attr == "snapshot")
            is_snapshot = true;
        }
      }
      // @require / @ensure / @snapshot (when imported as
      // `from icontract import require, ensure, snapshot`).
      else if(is_node_type(dec_func, "Name"))
      {
        std::string nm = json_string(json_member(dec_func, "id"));
        if(nm == "require")
          is_require = true;
        else if(nm == "ensure")
          is_ensure = true;
        else if(nm == "snapshot")
          is_snapshot = true;
      }
      if(!is_require && !is_ensure && !is_snapshot)
        continue;
      const jsont &dec_args = json_member(dec, "args");
      if(!dec_args.is_array() || as_array(dec_args).empty())
        continue;
      const jsont &first = *as_array(dec_args).begin();
      if(!is_node_type(first, "Lambda"))
        continue;
      if(is_require)
        icontract_require_lambdas.push_back(&first);
      else if(is_ensure)
        icontract_ensure_lambdas.push_back(&first);
      else
      {
        // @snapshot needs a name= keyword argument. Default
        // to empty (skip) if not provided.
        std::string snap_name;
        const jsont &dec_kws = json_member(dec, "keywords");
        if(dec_kws.is_array())
        {
          for(const auto &kw : as_array(dec_kws))
          {
            if(json_string(json_member(kw, "arg")) != "name")
              continue;
            const jsont &val = json_member(kw, "value");
            if(is_node_type(val, "Constant"))
            {
              const jsont &v = json_member(val, "value");
              if(v.is_string())
                snap_name = v.value;
            }
          }
        }
        if(snap_name.empty())
          continue;
        icontract_snapshots.emplace_back(std::move(snap_name), &first);
      }
    }
  }

  std::string func_name = json_string(json_member(stmt, "name"));
  source_locationt loc = get_location(stmt);

  // PLR §4.2.1: nested function definitions live in their
  // enclosing function's scope, not the module scope. Two
  // sibling functions can each contain `def f(...)` with
  // different bodies — they must not collide. We qualify
  // nested function names with the enclosing scope chain so
  // the symbol identifier is unique. Module-scope functions
  // keep their bare name for backwards-compat with module
  // imports and call resolution.
  std::string qualified_func_name = func_name;
  if(!current_function.empty())
    qualified_func_name = current_function + "::" + func_name;

  // Build parameter list
  const jsont &args_node = json_member(stmt, "args");
  const jsont &params = json_member(args_node, "args");

  code_typet::parameterst parameters;

  // PLR §8.7: positional-only parameters (before the '/' marker).
  // These are ordinary parameters from a call-site perspective; we
  // must still bind them so the function body can reference them.
  const jsont &posonlyargs = json_member(args_node, "posonlyargs");
  auto add_positional = [&](const jsont &param)
  {
    std::string param_name = json_string(json_member(param, "arg"));
    const jsont &annotation = json_member(param, "annotation");
    if(annotation.is_null())
    {
      log.warning() << "parameter '" << param_name << "' of function '"
                    << func_name << "' has no type annotation" << messaget::eom;
    }
    typet param_type = annotation.is_null()
                         ? python_value_type()
                         : convert_type_annotation(annotation);
    // Phase 7 type-annotation check: if the annotation is a
    // Union[X, Y, ...], extract its component types and
    // record under the parameter's symbol id so call-site
    // checks can detect arguments that don't match any
    // component.
    if(!annotation.is_null())
    {
      auto components = extract_union_components(annotation);
      if(!components.empty())
      {
        std::string param_id =
          "python::" + qualified_func_name + "::" + param_name;
        union_annotation_components[irep_idt{param_id}] = std::move(components);
      }
    }

    // PLR §4.2.1: Class instances are passed by reference.
    if(
      param_type.id() == ID_struct &&
      id2string(to_struct_type(param_type).get_tag()).find("python_class_") !=
        std::string::npos &&
      param_name != "self")
    {
      param_type = pointer_type(param_type);
    }

    // PLR §3.1: Mutable containers (list, dict) are also passed by
    // reference. Without this, mutations inside the function (e.g.
    // `xs.append(v)`) target a local copy and the caller's
    // container is silently unaffected, which is unsound w.r.t.
    // Python's reference semantics. The corresponding call-site
    // wraps the argument with address_of (the existing
    // 'struct arg -> pointer param' typecast path); inside the
    // body, convert_name auto-dereferences these pointer-typed
    // parameter symbols so existing member_exprt-based access
    // continues to work transparently.
    //
    // Self parameters are excluded above; varargs (*args) and
    // **kwargs are intentionally NOT wrapped here — they are
    // packed/freshly-built at the call site, so by-value vs
    // by-reference is moot, and pointer-wrapping them would
    // break the existing pack/unpack logic.
    if(
      param_name != "self" &&
      (is_python_list_type(param_type) || is_python_dict_type(param_type)))
    {
      param_type = pointer_type(param_type);
    }

    code_typet::parametert p{param_type};
    p.set_identifier("python::" + qualified_func_name + "::" + param_name);
    p.set_base_name(param_name);
    parameters.push_back(p);
  };

  if(posonlyargs.is_array())
  {
    for(const auto &param : as_array(posonlyargs))
      add_positional(param);
  }
  if(params.is_array())
  {
    for(const auto &param : as_array(params))
      add_positional(param);
  }

  // PLR §8.7: keyword-only arguments are appended later (after *args),
  // since per Python's parameter ordering, kwonlyargs follow the
  // bare * or *args separator. See block below after the vararg
  // handling for the actual append; this comment documents the
  // ordering decision.

  // PLR §8.7: *args — catch-all positional argument tuple.
  // We model it as a list (our tuple model is effectively a list here).
  const jsont &vararg = json_member(args_node, "vararg");
  std::string varargs_name;
  if(!vararg.is_null())
  {
    varargs_name = json_string(json_member(vararg, "arg"));
    typet va_type = python_list_type(python_value_type());
    code_typet::parametert p{va_type};
    p.set_identifier("python::" + qualified_func_name + "::" + varargs_name);
    p.set_base_name(varargs_name);
    // PLR §8.7: record the index of the *args param so the call
    // site can locate it for packing/unpacking even when closure
    // captures are appended later.
    function_vararg_index[irep_idt{"python::" + qualified_func_name}] =
      parameters.size();
    parameters.push_back(p);
  }

  // PLR §8.7: keyword-only arguments (anything after *args or a bare *).
  {
    const jsont &kwonlyargs = json_member(args_node, "kwonlyargs");
    if(kwonlyargs.is_array())
    {
      for(const auto &arg : as_array(kwonlyargs))
      {
        std::string param_name = json_string(json_member(arg, "arg"));
        typet ptype;
        const jsont &annot = json_member(arg, "annotation");
        if(!annot.is_null())
          ptype = convert_type_annotation(annot);
        else
          ptype = python_value_type();
        code_typet::parametert p{ptype};
        p.set_identifier("python::" + qualified_func_name + "::" + param_name);
        p.set_base_name(param_name);
        parameters.push_back(p);
      }
    }
  }

  // PLR §8.7: **kwargs — catch-all keyword argument dict
  const jsont &kwarg = json_member(args_node, "kwarg");
  std::string kwargs_name;
  if(!kwarg.is_null())
  {
    kwargs_name = json_string(json_member(kwarg, "arg"));
    typet kw_type = python_dict_type(python_string_type(), python_value_type());
    code_typet::parametert p{kw_type};
    p.set_identifier("python::" + qualified_func_name + "::" + kwargs_name);
    p.set_base_name(kwargs_name);
    parameters.push_back(p);
  }

  // Evaluate default parameter values at definition time (PLR §8.7)
  // Only for simple types — class instances use call-time evaluation
  {
    const jsont &defaults = json_member(args_node, "defaults");
    if(defaults.is_array() && !as_array(defaults).empty())
    {
      std::size_t n_defaults = as_array(defaults).size();
      std::size_t first_default = parameters.size() - n_defaults;
      auto def_it = as_array(defaults).begin();
      for(std::size_t i = first_default; i < parameters.size(); i++, ++def_it)
      {
        exprt val = convert_expression(*def_it);
        if(
          !val.is_nil() && val.type().id() != ID_struct &&
          val.type().id() != ID_pointer)
          default_values[{func_name, i}] = val;
      }
    }
  }
  // PLR §8.7: keyword-only parameter defaults are stored separately
  // in `kw_defaults` (positional defaults are in `defaults`). Each
  // entry is either an expression (default value) or null
  // (parameter is required). Match each entry to the kwonlyargs
  // parameter at the same index — kwonlyargs are appended to
  // `parameters` after the regular params and any *args slot, in
  // the order they appear.
  {
    const jsont &kwonly = json_member(args_node, "kwonlyargs");
    const jsont &kw_defaults = json_member(args_node, "kw_defaults");
    if(
      kwonly.is_array() && kw_defaults.is_array() &&
      as_array(kwonly).size() == as_array(kw_defaults).size())
    {
      // Find the first index of kwonlyargs in `parameters`. The
      // kwonlyargs were appended just after *args (or after the
      // regular params if no *args). We can scan parameters by
      // identifier-prefix to locate them.
      std::size_t kwonly_count = as_array(kwonly).size();
      std::size_t kwonly_start = parameters.size() - kwonly_count;
      auto kw_it = as_array(kwonly).begin();
      auto def_it = as_array(kw_defaults).begin();
      for(std::size_t k = 0; k < kwonly_count; k++, ++kw_it, ++def_it)
      {
        if(def_it->is_null())
          continue; // required kwonly param, no default
        exprt val = convert_expression(*def_it);
        if(
          !val.is_nil() && val.type().id() != ID_struct &&
          val.type().id() != ID_pointer)
          default_values[{func_name, kwonly_start + k}] = val;
      }
    }
  }

  // Return type
  bool has_yield = false;
  const jsont &returns = json_member(stmt, "returns");
  typet return_type = empty_typet{};
  if(!returns.is_null())
  {
    return_type = convert_type_annotation(returns);
    // Only register for missing-return checks when the
    // declared return type is NOT None. A function annotated
    // '-> None' legitimately falls through without returning
    // a value, and the implicit None return matches the
    // declared type. Detect None by inspecting the AST node
    // (Constant with value=None) directly so we don't have
    // to rely on the post-conversion typet which is now
    // python_int_type for None.
    bool is_none_annotation = false;
    if(is_node_type(returns, "Constant"))
    {
      const jsont &v = json_member(returns, "value");
      if(v.is_null())
        is_none_annotation = true;
    }
    if(!is_none_annotation)
      annotated_return_functions.insert(qualified_func_name);
  }
  else
  {
    // No return annotation — scan body for return/yield statements
    bool has_value_return = false;
    bool has_bare_return = false;
    bool has_none_return = false;
    // Inferred yield-element type for generator functions.
    // Starts as python_int_type; widens to float / str / bool
    // when the scanner observes a matching yield-literal.
    typet yield_element_type = python_int_type();
    std::function<void(const jsont &)> scan = [&](const jsont &body_node)
    {
      if(!body_node.is_array())
        return;
      for(const auto &s : as_array(body_node))
      {
        if(is_node_type(s, "Return"))
        {
          const jsont &rv = json_member(s, "value");
          if(rv.is_null())
            has_bare_return = true;
          else
          {
            has_value_return = true;
            // Detect explicit 'return None' — lets us infer
            // Optional[T] when a class-constructor return and a
            // None return coexist.
            if(
              is_node_type(rv, "Constant") &&
              json_member(rv, "value").is_null())
            {
              has_none_return = true;
            }
            else if(
              is_node_type(rv, "Name") &&
              json_string(json_member(rv, "id")) == "None")
            {
              has_none_return = true;
            }
            // Check if return value is a constructor call
            if(
              is_node_type(rv, "Call") &&
              is_node_type(json_member(rv, "func"), "Name"))
            {
              std::string call_name =
                json_string(json_member(json_member(rv, "func"), "id"));
              if(class_types.count(call_name))
              {
                typet this_type = class_types[call_name];
                if(return_type.id() == ID_empty)
                  return_type = this_type;
                else if(return_type != this_type)
                  // Multiple distinct class return types —
                  // Union[T1, T2] → widen to python_value_type.
                  // A concrete ClassInstance wraps to tag INT
                  // (non-None) in wrap_value, so the caller's
                  // 'is not None' check works; per-class
                  // dispatch inside the tagged union is still
                  // deferred (flagged as CLASS tag follow-up).
                  return_type = python_value_type();
              }
            }
            // PLR §6.10.5: 'return a, b' — the implicit tuple
            // is the return value. Infer the tuple type from
            // the syntactic shape (the element types are
            // approximated as python_int_type for unannotated
            // scalars; this matches how tuple literals get
            // their element types inferred elsewhere).
            if(is_node_type(rv, "Tuple") && return_type.id() == ID_empty)
            {
              const jsont &telts = json_member(rv, "elts");
              if(telts.is_array() && !as_array(telts).empty())
              {
                std::vector<typet> elem_types;
                for(const auto &e : as_array(telts))
                {
                  // Default to int; widen on Constant(float)
                  // or Constant(str). For Name operands, look
                  // up the symbol if it already exists (typed
                  // local variable assigned earlier in the
                  // body) and use its type. Anything else
                  // stays int and the call-site safe_typecast
                  // handles mismatches.
                  typet et = python_int_type();
                  if(is_node_type(e, "Constant"))
                  {
                    const jsont &cv = json_member(e, "value");
                    if(cv.is_string())
                      et = python_string_type();
                    else if(cv.is_number())
                    {
                      std::string vs = cv.value;
                      if(
                        vs.find('.') != std::string::npos ||
                        vs.find('e') != std::string::npos)
                        et = double_type();
                    }
                  }
                  else if(is_node_type(e, "Name"))
                  {
                    // Symbol may not exist yet (body not yet
                    // converted). Look up an enclosing
                    // AnnAssign for this name in the function
                    // body to get its declared type. If none,
                    // fall back to double_type — it can hold
                    // both ints and floats.
                    std::string nm = json_string(json_member(e, "id"));
                    irep_idt nid{"python::" + qualified_func_name + "::" + nm};
                    const symbolt *ns = symbol_table.lookup(nid);
                    if(ns != nullptr && ns->type.id() != ID_empty)
                      et = ns->type;
                    else
                    {
                      // Search the function body for AnnAssign
                      // 'nm: T = ...' to get the annotation.
                      const jsont &fn_body = json_member(stmt, "body");
                      if(fn_body.is_array())
                      {
                        for(const auto &bs : as_array(fn_body))
                        {
                          if(!is_node_type(bs, "AnnAssign"))
                            continue;
                          const jsont &target = json_member(bs, "target");
                          if(
                            !is_node_type(target, "Name") ||
                            json_string(json_member(target, "id")) != nm)
                            continue;
                          const jsont &ann = json_member(bs, "annotation");
                          if(!ann.is_null())
                            et = convert_type_annotation(ann);
                          break;
                        }
                      }
                      // Default fallback for unannotated locals:
                      // double_type covers both int and float
                      // promotions, which is sound for most
                      // tuple-return + isfinite/isnan patterns.
                      if(et == python_int_type())
                        et = double_type();
                    }
                  }
                  elem_types.push_back(et);
                }
                return_type = python_tuple_type(elem_types);
              }
            }
          }
        }
        // Detect yield (generator function) and infer the
        // yielded-value type from the yield expression. Used
        // below to pick the generator's list element type.
        if(is_node_type(s, "Expr"))
        {
          const jsont &val = json_member(s, "value");
          if(is_node_type(val, "Yield") || is_node_type(val, "YieldFrom"))
          {
            has_yield = true;
            const jsont &yield_val = json_member(val, "value");
            if(is_node_type(yield_val, "Constant"))
            {
              const jsont &cv = json_member(yield_val, "value");
              if(cv.is_number())
              {
                if(cv.value.find('.') != std::string::npos)
                  yield_element_type = double_type();
              }
              else if(cv.is_string())
                yield_element_type = python_string_type();
              else if(cv.is_true() || cv.is_false())
                yield_element_type = bool_typet{};
            }
          }
        }
        // Recurse into if/else/while/for/try bodies
        if(json_member(s, "body").is_array())
          scan(json_member(s, "body"));
        if(json_member(s, "orelse").is_array())
          scan(json_member(s, "orelse"));
        if(json_member(s, "handlers").is_array())
        {
          for(const auto &h : as_array(json_member(s, "handlers")))
            if(json_member(h, "body").is_array())
              scan(json_member(h, "body"));
        }
      }
    };
    scan(json_member(stmt, "body"));

    // Generator functions return a list of the inferred yield-
    // element type (eager-evaluation model).
    if(has_yield)
      return_type = python_list_type(yield_element_type);
    else if(
      has_value_return && has_none_return &&
      (return_type.id() == ID_struct_tag || return_type.id() == ID_struct))
    {
      // Optional[ClassName] — function returns either a class
      // instance or None. Widen to python_value_type so the
      // caller's 'is not None' check dispatches precisely on the
      // tagged union's tag.
      return_type = python_value_type();
    }
    else if(has_value_return && return_type.id() == ID_empty)
    {
      // If any parameter is float, return type is likely float
      bool has_float_param = false;
      for(const auto &p : parameters)
      {
        if(p.type().id() == ID_floatbv)
          has_float_param = true;
      }
      return_type = has_float_param ? double_type() : python_int_type();
    }
    else if(!has_value_return)
    {
      // PLR §7.6: a function that does not execute a value-returning
      // \`return\` statement falls off the end and returns None. Model
      // None as the int-typed sentinel so callers can distinguish it
      // from concrete values via \`r is None\`.
      // Only \`empty_typet\` is the "void" indicator that suppresses
      // the implicit return and makes the call's value undefined; we
      // want a real return slot.
      return_type = python_int_type();
    }
  }

  code_typet func_type{parameters, return_type};

  // Add closure captures as extra parameters
  irep_idt func_qid{"python::" + qualified_func_name};
  auto cap_it = closure_captures.find(id2string(func_qid));
  if(cap_it != closure_captures.end())
  {
    for(const auto &[outer_id, name, type] : cap_it->second)
    {
      code_typet::parametert p{type};
      std::string cap_param_id = "python::" + qualified_func_name + "::" + name;
      p.set_identifier(cap_param_id);
      p.set_base_name(name);
      parameters.push_back(p);
    }
    func_type = code_typet{parameters, return_type};
  }

  if(has_yield)
    generator_functions.insert(qualified_func_name);

  // Create function symbol BEFORE converting the body
  // (so recursive calls can find it)
  irep_idt symbol_id{"python::" + qualified_func_name};

  if(symbol_table.lookup(symbol_id) == nullptr)
  {
    symbolt func_symbol{symbol_id, func_type, "python"};
    func_symbol.base_name = func_name;
    func_symbol.location = loc;
    func_symbol.is_lvalue = true;
    symbol_table.add(func_symbol);
  }
  else
  {
    // Update pre-registered placeholder with real signature
    symbol_table.get_writeable_ref(symbol_id).type = func_type;
  }

  // Record @c_intrinsic mapping so convert_call can redirect to
  // the named C function instead of executing the Python body.
  if(is_c_intrinsic)
  {
    c_intrinsic_map[symbol_id] = c_intrinsic_name;
    if(!c_intrinsic_fold.empty())
      c_intrinsic_fold_map[symbol_id] = c_intrinsic_fold;
    if(!c_intrinsic_domain.empty())
      c_intrinsic_domain_map[symbol_id] = c_intrinsic_domain;
    if(!c_intrinsic_range.empty())
      c_intrinsic_range_map[symbol_id] = c_intrinsic_range;
    if(c_intrinsic_int_width == 32 || c_intrinsic_int_width == 64)
      c_intrinsic_int_width_map[symbol_id] = c_intrinsic_int_width;
  }

  // Create parameter symbols
  for(const auto &p : parameters)
  {
    symbolt param_symbol{p.get_identifier(), p.type(), "python"};
    param_symbol.base_name = p.get_base_name();
    param_symbol.location = loc;
    param_symbol.is_lvalue = true;
    param_symbol.is_state_var = true;
    param_symbol.is_parameter = true;
    if(symbol_table.lookup(param_symbol.name) == nullptr)
      symbol_table.add(param_symbol);
    else
    {
      // The module pre-scan may have created this symbol with a
      // different type (e.g. struct list instead of pointer-to-list).
      // Update it to match the function's actual parameter type.
      symbolt &existing = symbol_table.get_writeable_ref(param_symbol.name);
      if(existing.type != param_symbol.type)
        existing.type = param_symbol.type;
    }
  }

  // Convert function body
  std::string saved_function = current_function;
  auto saved_globals = global_names;
  if(!current_function.empty())
    enclosing_functions.push_back(current_function);
  current_function = qualified_func_name;
  global_names.clear();
  nonlocal_names.clear();

  // Phase 4 of the icontract integration plan: create the
  // `result` symbol that lambda bodies can reference to mean
  // the function's return value. Adding the symbol to the
  // function's scope lets convert_expression resolve
  // Name("result") to it during ensure-lambda translation
  // below. The post-process walk on body_block (further
  // down) assigns the actual return value to this symbol
  // before each assertion fires, so the postcondition sees
  // the right value. For void-like return types we still
  // create the symbol with python_int_type as a placeholder
  // — those ensures shouldn't reference result anyway.
  irep_idt result_symbol_id;
  if(!icontract_ensure_lambdas.empty())
  {
    typet rsym_type =
      return_type.id() == ID_empty ? python_int_type() : return_type;
    std::string rsym_name = "python::" + qualified_func_name + "::result";
    result_symbol_id = irep_idt{rsym_name};
    if(!symbol_table.has_symbol(result_symbol_id))
    {
      symbolt rsym{result_symbol_id, rsym_type, "python"};
      rsym.base_name = "result";
      rsym.is_lvalue = true;
      rsym.is_state_var = true;
      symbol_table.add(rsym);
    }
  }

  // Phase 5 of the icontract integration plan: translate
  // @snapshot decorators. For each @snapshot(lambda x:
  // expr_x, name="N") we:
  //   1. Translate expr_x to an exprt in the function's
  //      scope (so it can reference function parameters).
  //   2. Create a per-snapshot symbol
  //      python::FUNC::__icontract_old_N of the captured
  //      expression's type.
  //   3. Save the snapshot symbol's expression in
  //      active_old_snapshots[N] so that ensure-lambda
  //      translation below sees Name("OLD"), Attribute "N"
  //      and substitutes the snapshot symbol expression
  //      (handled in convert_attribute).
  //   4. Build the "capture at entry" assignments — these
  //      are pushed onto active_snapshot_assigns and
  //      injected into body_block as the first executable
  //      statements (after the require assumes) further
  //      below.
  std::map<std::string, exprt> saved_old_snapshots;
  saved_old_snapshots.swap(active_old_snapshots);
  std::vector<code_frontend_assignt> snapshot_assigns;
  for(const auto &snap : icontract_snapshots)
  {
    const std::string &name = snap.first;
    const jsont &lam = *snap.second;
    const jsont &lam_body = json_member(lam, "body");
    exprt cap;
    try
    {
      cap = convert_expression(lam_body);
    }
    catch(...)
    {
      cap = nil_exprt{};
    }
    if(cap.is_nil() || cap.type().id() == ID_empty)
      continue;
    std::string old_id_str =
      "python::" + qualified_func_name + "::__icontract_old_" + name;
    irep_idt old_id{old_id_str};
    if(!symbol_table.has_symbol(old_id))
    {
      symbolt old_sym{old_id, cap.type(), "python"};
      old_sym.base_name = "__icontract_old_" + name;
      old_sym.is_lvalue = true;
      old_sym.is_state_var = true;
      symbol_table.add(old_sym);
    }
    const symbolt &old_sym = symbol_table.lookup_ref(old_id);
    snapshot_assigns.push_back(
      code_frontend_assignt{old_sym.symbol_expr(), cap});
    active_old_snapshots[name] = old_sym.symbol_expr();
  }

  // Phase 3 of the icontract integration plan: translate the
  // @ensure lambdas now that current_function is set so Name
  // lookups in the lambda body resolve to function parameters
  // (Phase 3) and to the `result` symbol just registered above
  // (Phase 4). Successfully translated postconditions get
  // pushed onto active_ensures so the post-process walk can
  // emit the assertion before each return statement, and onto
  // the ID_C_spec_ensures slot on the function type for DFCC.
  std::vector<exprt> saved_ensures;
  saved_ensures.swap(active_ensures);
  for(const jsont *lam : icontract_ensure_lambdas)
  {
    const jsont &lam_body = json_member(*lam, "body");
    exprt cond;
    try
    {
      cond = convert_expression(lam_body);
    }
    catch(...)
    {
      cond = nil_exprt{};
    }
    if(cond.is_nil() || cond.type().id() == ID_empty)
      continue;
    if(cond.type().id() != ID_bool)
      cond = typecast_exprt{cond, bool_typet{}};
    active_ensures.push_back(cond);
    if(symbol_table.has_symbol(symbol_id))
    {
      typet &t = symbol_table.get_writeable_ref(symbol_id).type;
      static_cast<exprt &>(t.add(ID_C_spec_ensures)).operands().push_back(cond);
    }
  }

  // Pre-scan the body to collect parameter attribute uses.
  // Used by --python-check-any-arg-attrs at call sites to detect
  // Any-erasure bugs (cross-function flow where the caller has a
  // concrete class type for an arg whose corresponding parameter
  // is `Any`-typed).
  if(python_check_any_arg_attrs)
  {
    std::set<std::string> param_names;
    for(const auto &p : parameters)
    {
      // Only Any-typed parameters benefit from this analysis;
      // typed parameters already get dispatched correctly.
      if(is_python_value_type(p.type()))
        param_names.insert(std::string{id2string(p.get_base_name())});
    }
    if(!param_names.empty())
    {
      std::map<std::string, std::set<std::string>> per_param;
      const jsont &body_for_scan = json_member(stmt, "body");
      collect_param_attribute_uses(body_for_scan, param_names, per_param);
      for(const auto &kv : per_param)
      {
        irep_idt key{"python::" + qualified_func_name + "::" + kv.first};
        function_param_attr_uses[key] = kv.second;
      }
    }
  }

  // For generator functions, create __gen_result list
  bool is_generator = generator_functions.count(qualified_func_name) > 0;
  irep_idt gen_result_id;
  if(is_generator)
  {
    std::string grn = "__gen_result_" + qualified_func_name;
    std::string grq = qualify_name(grn);
    gen_result_id = irep_idt{grq};
    if(symbol_table.lookup(gen_result_id) == nullptr)
    {
      symbolt grs{gen_result_id, return_type, "python"};
      grs.base_name = grn;
      grs.is_lvalue = true;
      grs.is_state_var = true;
      symbol_table.add(grs);
    }
  }

  code_blockt body_block;

  // For generators, initialize __gen_result.length = 0 and zero
  // the data buffer so list-equality with a literal works for
  // values beyond the populated length (PLR §6.2.9 + §6.10.1).
  if(is_generator)
  {
    body_block.add(code_frontend_assignt{
      member_exprt{
        symbol_table.lookup_ref(gen_result_id).symbol_expr(),
        "length",
        signedbv_typet{64}},
      from_integer(0, signedbv_typet{64})});
    // Zero the data array via an assignment to a fresh
    // zero-initialised array of the same type.
    if(return_type.id() == ID_struct)
    {
      const auto &rt = to_struct_type(return_type);
      if(rt.components().size() >= 2)
      {
        const auto &data_t = to_array_type(rt.components()[1].type());
        exprt::operandst zero_elems;
        while(zero_elems.size() < PYTHON_MAX_LIST_LENGTH)
          zero_elems.push_back(safe_zero(data_t.element_type()));
        body_block.add(code_frontend_assignt{
          member_exprt{
            symbol_table.lookup_ref(gen_result_id).symbol_expr(),
            "data",
            data_t},
          array_exprt{std::move(zero_elems), data_t}});
      }
    }
  }

  // Phase 2 of the icontract integration plan: lower each
  // @require lambda to (a) a __CPROVER_assume at the start of
  // the function body so the body is verified under the
  // precondition, and (b) a __CPROVER_requires clause on the
  // function type so DFCC can use it for caller-side checking.
  // We invoke this AFTER param symbols are added (so Name
  // lookups in the lambda body resolve to function params) and
  // AFTER current_function is set to qualified_func_name, but
  // BEFORE the body conversion that consumes them. The
  // translation gracefully degrades to a skip when the lambda
  // body can't be converted (e.g. references a free variable
  // not in the function's signature).
  for(const jsont *lam : icontract_require_lambdas)
  {
    const jsont &lam_body = json_member(*lam, "body");
    exprt cond;
    try
    {
      cond = convert_expression(lam_body);
    }
    catch(...)
    {
      cond = nil_exprt{};
    }
    if(cond.is_nil() || cond.type().id() == ID_empty)
      continue;
    if(cond.type().id() != ID_bool)
      cond = typecast_exprt{cond, bool_typet{}};
    body_block.add(code_assumet{cond});
    // Attach to the function type for DFCC compatibility.
    if(symbol_table.has_symbol(symbol_id))
    {
      typet &t = symbol_table.get_writeable_ref(symbol_id).type;
      static_cast<exprt &>(t.add(ID_C_spec_requires))
        .operands()
        .push_back(cond);
    }
  }

  // Phase 5 of the icontract integration plan: emit the
  // @snapshot capture assignments at function entry, after
  // the precondition assumes (so the captures see the
  // post-assume state — which equals the pre-state under
  // the precondition).
  for(auto &assign : snapshot_assigns)
    body_block.add(std::move(assign));

  const jsont &body = json_member(stmt, "body");

  // Detect closures: scan for nested FunctionDefs that reference
  // our parameters or local variables (free variables).
  if(body.is_array())
  {
    // Collect our parameters
    std::set<std::string> our_params;
    for(const auto &p : parameters)
      our_params.insert(std::string{id2string(p.get_base_name())});

    // Also collect local variable names (assigned in our body)
    // These are Name targets in Assign/AnnAssign statements
    for(const auto &s : as_array(body))
    {
      if(is_node_type(s, "AnnAssign"))
      {
        const jsont &tgt = json_member(s, "target");
        if(is_node_type(tgt, "Name"))
          our_params.insert(json_string(json_member(tgt, "id")));
      }
      else if(is_node_type(s, "Assign"))
      {
        const jsont &tgts = json_member(s, "targets");
        if(tgts.is_array())
        {
          for(const auto &t : as_array(tgts))
          {
            if(is_node_type(t, "Name"))
              our_params.insert(json_string(json_member(t, "id")));
          }
        }
      }
    }

    for(const auto &s : as_array(body))
    {
      if(is_node_type(s, "FunctionDef") || is_node_type(s, "AsyncFunctionDef"))
      {
        std::string nested_bare = json_string(json_member(s, "name"));
        // PLR §4.2.1: nested function symbol ids are qualified
        // by the enclosing scope chain (see top of
        // convert_function_def). Mirror that when keying the
        // closure_captures map so the inner conversion can find
        // its captures.
        std::string nested_name = current_function + "::" + nested_bare;
        std::set<std::string> nested_params = collect_param_names(s);
        // Collect names declared 'nonlocal' or 'global' inside the
        // nested function — these must not be captured by value;
        // they resolve through qualify_name's nonlocal/global
        // redirect directly to the enclosing/module scope.
        std::set<std::string> nested_nonlocal_global;
        const jsont &nbody = json_member(s, "body");
        if(nbody.is_array())
        {
          for(const auto &ns : as_array(nbody))
          {
            if(is_node_type(ns, "Nonlocal") || is_node_type(ns, "Global"))
            {
              const jsont &nnames = json_member(ns, "names");
              if(nnames.is_array())
              {
                for(const auto &n : as_array(nnames))
                  if(n.is_string())
                    nested_nonlocal_global.insert(n.value);
              }
            }
          }
        }
        std::set<std::string> refs;
        collect_name_refs(json_member(s, "body"), refs);
        std::vector<std::tuple<std::string, std::string, typet>> captures;
        for(const auto &ref : refs)
        {
          if(nested_params.count(ref) || !our_params.count(ref))
            continue;
          if(nested_nonlocal_global.count(ref))
            continue; // handled by qualify_name redirect at use site
          // Find the variable's symbol and type
          std::string var_id = "python::" + qualified_func_name + "::" + ref;
          const symbolt *var_sym = symbol_table.lookup(irep_idt{var_id});
          if(var_sym == nullptr)
          {
            // Try as a parameter
            bool found = false;
            for(const auto &p : parameters)
            {
              if(id2string(p.get_base_name()) == ref)
              {
                captures.push_back(
                  {id2string(p.get_identifier()), ref, p.type()});
                found = true;
                break;
              }
            }
            // Local variable not yet in symbol table — create it
            if(!found)
            {
              typet var_type = python_int_type();
              // Try to infer type from AnnAssign annotation
              for(const auto &bs : as_array(body))
              {
                if(is_node_type(bs, "AnnAssign"))
                {
                  const jsont &tgt = json_member(bs, "target");
                  if(
                    is_node_type(tgt, "Name") &&
                    json_string(json_member(tgt, "id")) == ref)
                  {
                    var_type =
                      convert_type_annotation(json_member(bs, "annotation"));
                    break;
                  }
                }
              }
              // Create the symbol so it exists for the nested function
              if(symbol_table.lookup(irep_idt{var_id}) == nullptr)
              {
                symbolt new_sym{irep_idt{var_id}, var_type, "python"};
                new_sym.base_name = ref;
                new_sym.is_lvalue = true;
                new_sym.is_state_var = true;
                symbol_table.add(new_sym);
              }
              captures.push_back({var_id, ref, var_type});
            }
          }
          else
          {
            captures.push_back({var_id, ref, var_sym->type});
          }
        }
        if(!captures.empty())
          closure_captures["python::" + nested_name] = captures;
      }
    }
  }

  if(body.is_array())
  {
    // PLR §3.1: pre-scan to identify names that escape into a
    // container literal (or list-mutating method). This must run
    // BEFORE convert_statement(s) so that the literal-construction
    // path knows to wrap the escaped Names with python_value
    // pointers rather than struct-copying them.
    collect_escaped_mutables(body);
    for(const auto &s : as_array(body))
      body_block.add(convert_statement(s));
  }

  // Phase 3 + 4 of the icontract integration plan: walk the
  // body recursively and replace each code_frontend_returnt
  // with:
  //   { result = X; assert(ensures); return X; }
  // so the postcondition assertions see the return value
  // bound to the `result` symbol. For bare returns (no
  // value) we just prepend the assertions. This catches all
  // return statements no matter how deeply nested (inside
  // if / for / while / try blocks). The implicit fall-through
  // return below is handled separately.
  if(!active_ensures.empty())
  {
    std::function<void(codet &)> inject_at_returns = [&](codet &c) -> void
    {
      // Walk operands; if an operand is a code_frontend_returnt,
      // replace it with a code_blockt(assign-result + asserts + return).
      // Otherwise recurse into operands that are codet.
      for(auto &op : c.operands())
      {
        if(op.id() != ID_code)
          continue;
        codet &inner = static_cast<codet &>(op);
        if(inner.get_statement() == ID_return)
        {
          const code_frontend_returnt &ret =
            static_cast<const code_frontend_returnt &>(inner);
          code_blockt blk;
          // Assign return value to `result` symbol so postcondition
          // assertions can reference it. Skip for bare returns
          // (no return value) and when the result symbol wasn't
          // registered (no ensure decorator collected one).
          if(
            ret.has_return_value() && !result_symbol_id.empty() &&
            symbol_table.has_symbol(result_symbol_id))
          {
            const symbolt &rsym = symbol_table.lookup_ref(result_symbol_id);
            exprt rv = ret.return_value();
            if(rv.type() != rsym.type)
              rv = safe_typecast(rv, rsym.type);
            blk.add(code_frontend_assignt{rsym.symbol_expr(), rv});
          }
          for(const exprt &cond : active_ensures)
            blk.add(code_assertt{cond});
          blk.add(static_cast<const codet &>(inner));
          op = std::move(blk);
        }
        else
        {
          inject_at_returns(inner);
        }
      }
    };
    inject_at_returns(body_block);
  }

  current_function = saved_function;
  global_names = saved_globals;
  if(
    !enclosing_functions.empty() &&
    enclosing_functions.back() == saved_function)
    enclosing_functions.pop_back();

  // PLR §7.6: if function doesn't end with return, append return
  // For generators: return __gen_result list
  if(is_generator)
  {
    // Phase 3 icontract: assert ensures before the implicit
    // generator-result return.
    for(const exprt &cond : active_ensures)
      body_block.add(code_assertt{cond});
    body_block.add(code_frontend_returnt{
      symbol_table.lookup_ref(gen_result_id).symbol_expr()});
  }
  else if(return_type.id() != ID_empty)
  {
    // Re-read the function's return type from the symbol
    // table; convert_return may have widened it during body
    // conversion (e.g. annotated `-> int` widened to
    // python_value when the body returns a tagged-union
    // value). Without this, the implicit fall-through
    // None-encoded as a python_int sentinel gets silently
    // typecast to python_value at goto-conversion, emitting
    // a "warning: ignoring typecast" and leaving the
    // exception-active early-exit path with a wrong-typed
    // return value.
    const symbolt *cur_func_sym =
      symbol_table.lookup(irep_idt{"python::" + qualified_func_name});
    if(cur_func_sym != nullptr && cur_func_sym->type.id() == ID_code)
    {
      const typet &updated_rt = to_code_type(cur_func_sym->type).return_type();
      if(updated_rt.id() != ID_empty)
        return_type = updated_rt;
    }
    exprt none_expr;
    if(is_python_value_type(return_type))
      none_expr = make_python_value(
        python_type_tagt::NONE, from_integer(0, signedbv_typet{64}));
    else
    {
      mp_integer none_val = mp_integer(1) << 62;
      none_val = -none_val;
      none_expr =
        safe_typecast(from_integer(none_val, python_int_type()), return_type);
    }
    // Phase 3 icontract: assert ensures before the implicit
    // None return.
    for(const exprt &cond : active_ensures)
      body_block.add(code_assertt{cond});
    // --python-missing-return-check: when this function has
    // an explicit non-None return-type annotation but a
    // control-flow path reaches the implicit fall-through,
    // emit a missing-return property. Reachability of this
    // assertion implies the path didn't execute a `return X`
    // — which is a Python bug since the implicit None return
    // violates the declared return type.
    //
    // The check is gated on annotated_return_functions to
    // avoid firing for functions with no annotation (where
    // the inferred-int-return is just our default).
    if(
      python_missing_return_check &&
      annotated_return_functions.count(qualified_func_name) > 0)
    {
      source_locationt mr_loc = loc;
      mr_loc.set_property_class("missing-return");
      mr_loc.set_comment(
        "function '" + qualified_func_name +
        "' has annotated return type but a path reaches the "
        "implicit fall-through without returning a value");
      code_assertt mr{false_exprt{}};
      mr.add_source_location() = mr_loc;
      body_block.add(std::move(mr));
    }
    body_block.add(code_frontend_returnt{none_expr});
  }
  else
  {
    // Empty return type (void-like). Still emit ensures
    // assertions before the implicit fall-through.
    for(const exprt &cond : active_ensures)
      body_block.add(code_assertt{cond});
  }

  // Update the symbol with the body
  symbolt *sym_ptr = symbol_table.get_writeable(symbol_id);
  if(sym_ptr != nullptr)
    sym_ptr->value = body_block;

  // Restore the parent function's icontract ensures (Phase 3 of
  // the integration plan). Done AFTER body assembly because the
  // implicit-return injection above reads from active_ensures.
  active_ensures.swap(saved_ensures);
  active_old_snapshots.swap(saved_old_snapshots);

  // PLR §8.7: if the AST body is a single \`return <constant>\`, record
  // the constant for downstream propagation. Only the simplest shape
  // qualifies — single statement, Return AST node, value is a Constant
  // node (or UnaryOp(USub, Constant) for negative literals). This
  // covers the \`def f() -> int: return 97\` pattern where every call
  // returns the same value, and lets \`c = f()\` look up f's constant
  // through try_eval_double for further folding (chr(c), len(...) on
  // a constant-length list, math.X on a constant double, etc.).
  if(body.is_array() && as_array(body).size() == 1)
  {
    const jsont &only_stmt = *as_array(body).begin();
    if(is_node_type(only_stmt, "Return"))
    {
      const jsont &val = json_member(only_stmt, "value");
      if(!val.is_null())
      {
        // convert_expression would normally honour the surrounding
        // function scope; we're past current_function = saved here so
        // any reference to a parameter would mis-resolve. Restrict to
        // pure-constant shapes.
        bool is_const =
          is_node_type(val, "Constant") ||
          (is_node_type(val, "UnaryOp") &&
           is_node_type(json_member(val, "operand"), "Constant"));
        if(is_const)
        {
          exprt val_expr = convert_expression(val);
          auto evd = try_eval_double(val_expr);
          if(evd.has_value())
            function_return_constants[symbol_id] = evd.value();
        }
      }
    }
  }

  // Function definitions don't produce executable code at the call site
  // UNLESS the function has decorators (PLR §8.7).
  //
  // PLR §8.7: "A function definition may be wrapped by one or more
  // decorator expressions. (...) The result is then bound to the
  // function name instead of the function object."
  //
  // @dec
  // def f(x): ...
  //
  // is equivalent to:
  //
  // def f(x): ...
  // f = dec(f)
  //
  // For multiple decorators they are applied bottom-up:
  // @dec1 @dec2 def f(): ... → f = dec1(dec2(f))
  //
  // We emit the decorator call(s) as pending_checks so they fire
  // at the statement site where the function definition appears.
  // The function_aliases map is updated to point at the wrapper
  // so subsequent calls to `f` dispatch through the decorator.
  if(decorators.is_array() && !is_c_intrinsic)
  {
    // Collect non-overload, non-c_intrinsic decorators (bottom-up order
    // = reverse of the list in the AST, which is top-down).
    std::vector<const jsont *> user_decorators;
    for(const auto &dec : as_array(decorators))
    {
      if(
        is_node_type(dec, "Name") &&
        json_string(json_member(dec, "id")) == "overload")
        continue;
      if(
        is_node_type(dec, "Name") &&
        (json_string(json_member(dec, "id")) == "staticmethod" ||
         json_string(json_member(dec, "id")) == "classmethod" ||
         json_string(json_member(dec, "id")) == "property"))
        continue;
      if(is_node_type(dec, "Call"))
      {
        const jsont &df = json_member(dec, "func");
        if(
          is_node_type(df, "Name") &&
          json_string(json_member(df, "id")) == "c_intrinsic")
          continue;
      }
      user_decorators.push_back(&dec);
    }
    if(!user_decorators.empty())
    {
      // Build the chain: start with the raw function, apply each
      // decorator from bottom (last in list) to top (first).
      // For each decorator, look up its symbol and emit a call.
      // The final result is aliased to the function name.
      code_blockt dec_block;
      for(auto rit = user_decorators.rbegin(); rit != user_decorators.rend();
          ++rit)
      {
        const jsont &dec = **rit;
        exprt dec_expr = convert_expression(dec);
        if(dec_expr.is_nil() || dec_expr.type().id() != ID_code)
          continue;
        // The decorator is a function. Call it with the current
        // function as argument. The result becomes the new binding.
        // We model this by recording the decorator's inner function
        // (if it returns one) as the alias target. For the common
        // pattern where the decorator defines a `wrapper` inside and
        // returns it, the wrapper is already registered as a symbol
        // by convert_function_def (since it's a nested def inside
        // the decorator body). We look for it by convention:
        // decorator_name::wrapper.
        std::string dec_name =
          id2string(to_symbol_expr(dec_expr).get_identifier());
        // Look for a nested function named "wrapper" inside the decorator.
        // The frontend registers nested functions with various naming
        // conventions; try the most common ones.
        irep_idt wrapper_id;
        for(const std::string &candidate :
            {dec_name + "::wrapper",
             std::string{"python::wrapper"},
             std::string{"python::" + func_name + "::wrapper"}})
        {
          if(symbol_table.lookup(irep_idt{candidate}) != nullptr)
          {
            wrapper_id = irep_idt{candidate};
            break;
          }
        }
        // Also scan the decorator's body AST for a nested FunctionDef
        // named "wrapper" and use whatever symbol name it got.
        // PLR §8.7: decorators may be defined inside another
        // function (e.g. a test harness that defines `my_dec`
        // and then `@my_dec def f(): ...` all inside one
        // outer function). We therefore search the parse tree
        // RECURSIVELY for the decorator's FunctionDef node,
        // not just the module body.
        std::function<const jsont *(const jsont &, const std::string &)>
          find_func_def =
            [&](const jsont &scope_body,
                const std::string &name) -> const jsont * {
          if(!scope_body.is_array())
            return nullptr;
          for(const auto &s : as_array(scope_body))
          {
            if(
              is_node_type(s, "FunctionDef") &&
              json_string(json_member(s, "name")) == name)
              return &s;
            // Recurse into nested function bodies to find
            // decorators defined inside other functions.
            if(is_node_type(s, "FunctionDef") || is_node_type(s, "ClassDef"))
            {
              const jsont &b = json_member(s, "body");
              if(const jsont *r = find_func_def(b, name))
                return r;
            }
            // Recurse into if/for/while/try bodies too, in case
            // the decorator is defined inside a control-flow block.
            for(const std::string &fld :
                std::vector<std::string>{
                  "body", "orelse", "finalbody", "handlers"})
            {
              const jsont &fb = json_member(s, fld);
              if(fb.is_array())
                if(const jsont *r = find_func_def(fb, name))
                  return r;
            }
          }
          return nullptr;
        };
        if(wrapper_id.empty())
        {
          std::string dec_short =
            id2string(to_symbol_expr(dec_expr).get_identifier()).substr(8);
          const jsont *dec_ast =
            find_func_def(json_member(parse_tree.ast_json, "body"), dec_short);
          if(dec_ast != nullptr)
          {
            const jsont &dec_body = json_member(*dec_ast, "body");
            if(dec_body.is_array())
            {
              for(const auto &inner : as_array(dec_body))
              {
                if(is_node_type(inner, "FunctionDef"))
                {
                  std::string inner_name =
                    json_string(json_member(inner, "name"));
                  irep_idt cand{"python::" + inner_name};
                  if(symbol_table.lookup(cand) != nullptr)
                  {
                    wrapper_id = cand;
                    break;
                  }
                }
              }
            }
          }
        }
        const symbolt *wrapper_sym =
          wrapper_id.empty() ? nullptr : symbol_table.lookup(wrapper_id);
        if(wrapper_sym != nullptr && wrapper_sym->type.id() == ID_code)
        {
          // Redirect calls to f to go through wrapper instead.
          // The qualified symbol_id covers callers using the
          // qualified id directly. We additionally register an
          // alias keyed by the BARE name `python::<func_name>`
          // to support callers that haven't switched to qualified
          // ids (e.g. module-level dispatch through a closure
          // alias). For nested functions, callers inside the same
          // enclosing function use `python::<enclosing>::<func>`,
          // which equals symbol_id and is already covered.
          function_aliases[id2string(symbol_id)] = wrapper_id;
          if(!saved_function.empty() && saved_function != current_function)
            function_aliases["python::" + func_name] = wrapper_id;
          // The wrapper's body calls `fn(*args)` where `fn` is a
          // closure-captured reference to the original function.
          // Bind `fn` (the decorator's parameter name) to the
          // original function, then RE-CONVERT the wrapper's body
          // so the call resolves correctly. The first conversion
          // happened when the decorator was defined (before we knew
          // what fn would be), so it emitted "no body for callee fn".
          const code_typet &dec_type =
            to_code_type(symbol_table.lookup_ref(dec_name).type);
          if(!dec_type.parameters().empty())
          {
            std::string param_base =
              id2string(dec_type.parameters()[0].get_base_name());
            // Bind fn → original f in all scopes the wrapper might
            // look it up from.
            function_aliases["python::" + param_base] = symbol_id;
            function_aliases[id2string(wrapper_id) + "::" + param_base] =
              symbol_id;
            function_aliases[id2string(
              dec_type.parameters()[0].get_identifier())] = symbol_id;
          }
          // Re-convert the wrapper's body now that fn is bound.
          // Find the wrapper's AST in the decorator's body — search
          // RECURSIVELY through the parse tree so decorators
          // defined inside another function are also handled.
          std::function<const jsont *(const jsont &, const std::string &)>
            find_decorator_ast =
              [&](const jsont &scope_body,
                  const std::string &name) -> const jsont * {
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
                const jsont &b = json_member(s, "body");
                if(const jsont *r = find_decorator_ast(b, name))
                  return r;
              }
              for(const std::string &fld :
                  std::vector<std::string>{
                    "body", "orelse", "finalbody", "handlers"})
              {
                const jsont &fb = json_member(s, fld);
                if(fb.is_array())
                  if(const jsont *r = find_decorator_ast(fb, name))
                    return r;
              }
            }
            return nullptr;
          };
          std::string dec_short =
            id2string(to_symbol_expr(dec_expr).get_identifier()).substr(8);
          // PLR §4.2.1: when the decorator itself is nested
          // (e.g. `def block_0(): def my_dec(...): ...`), its
          // qualified id is `python::block_0::my_dec`. The AST
          // node has the bare name "my_dec" — extract the last
          // `::` component to match against the AST.
          std::string dec_short_bare = dec_short;
          if(auto pos = dec_short.rfind("::"); pos != std::string::npos)
            dec_short_bare = dec_short.substr(pos + 2);
          const jsont *dec_ast = find_decorator_ast(
            json_member(parse_tree.ast_json, "body"), dec_short_bare);
          if(dec_ast != nullptr)
          {
            const jsont &dec_body_ast = json_member(*dec_ast, "body");
            if(dec_body_ast.is_array())
            {
              for(const auto &inner : as_array(dec_body_ast))
              {
                if(!is_node_type(inner, "FunctionDef"))
                  continue;
                std::string inner_name =
                  json_string(json_member(inner, "name"));
                // Build the qualified id of the inner function:
                // it's a nested def inside the decorator, so its
                // symbol id is `python::<dec_short>::<inner_name>`.
                irep_idt inner_qid{"python::" + dec_short + "::" + inner_name};
                if(inner_qid != wrapper_id)
                  continue;
                // Re-convert the wrapper function body
                std::string saved_fn = current_function;
                // Use the wrapper's qualified id (without the
                // "python::" prefix) so qualify_name() and the
                // closure-capture mechanism resolve names against
                // the right scope.
                current_function = id2string(wrapper_id).substr(8);
                const jsont &wrapper_body_ast = json_member(inner, "body");
                code_blockt new_body;
                if(wrapper_body_ast.is_array())
                {
                  for(const auto &ws : as_array(wrapper_body_ast))
                    new_body.add(convert_statement(ws));
                }
                // Append default return
                const code_typet &wt = to_code_type(wrapper_sym->type);
                if(wt.return_type().id() != ID_empty)
                  new_body.add(
                    code_frontend_returnt{safe_zero(wt.return_type())});
                current_function = saved_fn;
                symbol_table.get_writeable_ref(wrapper_id).value = new_body;
                break;
              }
            }
          }
        }
      }
      if(!dec_block.statements().empty())
        return std::move(dec_block);
    }
  }

  return code_skipt{};
}

// PLR §8.9: Class definitions
// PLR §3.2: "Class instances have a namespace implemented as a dictionary."
// We model classes as structs with __class_tag for dispatch.
// "A class definition defines a class object."
codet python_convertert::convert_class_def(const jsont &stmt)
{
  std::string class_name = json_string(json_member(stmt, "name"));
  source_locationt loc = get_location(stmt);

  // Phase 6 of the icontract integration plan
  // (doc/python-frontend-icontract-plan.md): collect class-level
  // @icontract.invariant decorators. Each invariant lambda is
  // asserted at the entry and exit of every public method (with
  // a special exception for __init__ which only gets the exit
  // assertion since the object doesn't exist at entry). We store
  // the lambda body pointers here; the actual translation
  // happens inside each method's conversion scope further down,
  // where `self` resolves to the method's first parameter.
  //
  // Phase 7 (Liskov inheritance composition): walk the bases
  // and prepend the parent classes' invariants. Per Liskov
  // substitution, child invariants merge with parents' via
  // AND — the child must satisfy every invariant the parent
  // declared. We achieve this by including the parent's
  // lambdas in the child's effective set; each method's body
  // then asserts all of them. The walk uses
  // class_invariant_lambdas which is populated as each class
  // is processed (parent before child by Python's source-order
  // requirement).
  std::vector<const jsont *> icontract_invariant_lambdas;
  {
    const jsont &cls_decorators = json_member(stmt, "decorator_list");
    if(cls_decorators.is_array())
    {
      for(const auto &dec : as_array(cls_decorators))
      {
        if(!is_node_type(dec, "Call"))
          continue;
        const jsont &dec_func = json_member(dec, "func");
        bool is_inv = false;
        if(is_node_type(dec_func, "Attribute"))
        {
          const jsont &v = json_member(dec_func, "value");
          if(
            is_node_type(v, "Name") &&
            json_string(json_member(v, "id")) == "icontract" &&
            json_string(json_member(dec_func, "attr")) == "invariant")
            is_inv = true;
        }
        else if(
          is_node_type(dec_func, "Name") &&
          json_string(json_member(dec_func, "id")) == "invariant")
          is_inv = true;
        if(!is_inv)
          continue;
        const jsont &dec_args = json_member(dec, "args");
        if(!dec_args.is_array() || as_array(dec_args).empty())
          continue;
        const jsont &first = *as_array(dec_args).begin();
        if(is_node_type(first, "Lambda"))
          icontract_invariant_lambdas.push_back(&first);
      }
    }
  }
  // Phase 7: prepend parent classes' invariants. We walk the
  // explicit bases in source order; each base's effective
  // invariants are already composed (recursive merge happened
  // when the base was processed) so we don't need to re-walk
  // grandparents.
  {
    const jsont &cls_bases = json_member(stmt, "bases");
    if(cls_bases.is_array())
    {
      std::vector<const jsont *> inherited;
      for(const auto &base : as_array(cls_bases))
      {
        if(!is_node_type(base, "Name"))
          continue;
        std::string base_name = json_string(json_member(base, "id"));
        auto it = class_invariant_lambdas.find(base_name);
        if(it == class_invariant_lambdas.end())
          continue;
        for(const jsont *lam : it->second)
          inherited.push_back(lam);
      }
      // Inherited invariants come first so they appear in a
      // predictable order in the generated assertions.
      if(!inherited.empty())
      {
        inherited.insert(
          inherited.end(),
          icontract_invariant_lambdas.begin(),
          icontract_invariant_lambdas.end());
        icontract_invariant_lambdas = std::move(inherited);
      }
    }
  }
  // Register this class's effective invariant lambdas (own +
  // inherited) so future subclasses can inherit them.
  if(!icontract_invariant_lambdas.empty())
    class_invariant_lambdas[class_name] = icontract_invariant_lambdas;

  // Analyze __init__ to determine instance attributes
  struct_typet::componentst components;
  const jsont &body = json_member(stmt, "body");

  const jsont *init_method = nullptr;
  if(body.is_array())
  {
    for(const auto &item : as_array(body))
    {
      if(
        (is_node_type(item, "FunctionDef") ||
         is_node_type(item, "AsyncFunctionDef")) &&
        json_string(json_member(item, "name")) == "__init__")
      {
        init_method = &item;
        break;
      }
    }
  }

  // PLR §8.9: Inherit fields from base classes (supports multiple inheritance)
  const jsont &bases = json_member(stmt, "bases");
  std::set<std::string> inherited_fields;
  // All field names already added to `components` (regardless of
  // origin). Used by the class-level / __init__ scanners below to
  // avoid emitting duplicate field declarations — the SMT-LIB
  // back-end (smt2_conv) lowers each component as a datatype
  // selector, and CVC5 rejects datatypes whose constructor has
  // two selectors with the same name. Pre-populated with the
  // names already inherited so subclass scans skip them too.
  std::set<std::string> declared_fields;
  if(bases.is_array())
  {
    for(const auto &base : as_array(bases))
    {
      if(is_node_type(base, "Name"))
      {
        std::string base_name = json_string(json_member(base, "id"));
        if(class_types.count(base_name))
        {
          const auto &base_type = class_types[base_name];
          for(const auto &comp : base_type.components())
          {
            std::string fname = id2string(comp.get_name());
            // Skip __class_tag and already-inherited fields (diamond)
            if(fname == "__class_tag" || inherited_fields.count(fname))
              continue;
            inherited_fields.insert(fname);
            declared_fields.insert(fname);
            components.push_back(comp);
          }
        }
      }
    }
  }

  // Scan class body for class-level attributes (AnnAssign outside methods)
  if(body.is_array())
  {
    for(const auto &item : as_array(body))
    {
      if(is_node_type(item, "AnnAssign"))
      {
        const jsont &target = json_member(item, "target");
        if(is_node_type(target, "Name"))
        {
          std::string attr_name = json_string(json_member(target, "id"));
          if(declared_fields.insert(attr_name).second)
          {
            typet attr_type =
              convert_type_annotation(json_member(item, "annotation"));
            components.push_back(
              struct_typet::componentt{attr_name, attr_type});
          }
        }
      }
      else if(is_node_type(item, "Assign"))
      {
        const jsont &targets = json_member(item, "targets");
        if(targets.is_array())
        {
          for(const auto &t : as_array(targets))
          {
            if(is_node_type(t, "Name"))
            {
              std::string attr_name = json_string(json_member(t, "id"));
              if(!declared_fields.insert(attr_name).second)
                continue;
              // Infer type from value
              const jsont &val = json_member(item, "value");
              typet attr_type = python_int_type();
              if(is_node_type(val, "Constant"))
              {
                const jsont &v = json_member(val, "value");
                if(v.is_string())
                  attr_type = python_string_type();
                else if(v.is_true() || v.is_false())
                  attr_type = python_int_type();
              }
              components.push_back(
                struct_typet::componentt{attr_name, attr_type});
            }
          }
        }
      }
    }
  }

  // Scan __init__ body for self.attr = ... assignments to determine fields
  if(init_method != nullptr)
  {
    const jsont &init_body = json_member(*init_method, "body");
    if(init_body.is_array())
    {
      for(const auto &s : as_array(init_body))
      {
        // Handle AnnAssign: self.attr: Type = value
        if(is_node_type(s, "AnnAssign"))
        {
          const jsont &target = json_member(s, "target");
          if(!is_node_type(target, "Attribute"))
            continue;
          const jsont &target_value = json_member(target, "value");
          if(
            !is_node_type(target_value, "Name") ||
            json_string(json_member(target_value, "id")) != "self")
            continue;

          std::string attr_name = json_string(json_member(target, "attr"));
          const jsont &annotation = json_member(s, "annotation");
          typet attr_type = annotation.is_null()
                              ? python_value_type()
                              : convert_type_annotation(annotation);
          if(declared_fields.insert(attr_name).second)
            components.push_back(
              struct_typet::componentt{attr_name, attr_type});
          continue;
        }

        if(!is_node_type(s, "Assign"))
          continue;
        const jsont &targets = json_member(s, "targets");
        if(!targets.is_array() || as_array(targets).empty())
          continue;
        const jsont &target = *as_array(targets).begin();
        if(!is_node_type(target, "Attribute"))
          continue;
        const jsont &target_value = json_member(target, "value");
        if(
          !is_node_type(target_value, "Name") ||
          json_string(json_member(target_value, "id")) != "self")
          continue;

        std::string attr_name = json_string(json_member(target, "attr"));

        // Determine type from the __init__ parameter annotation
        // Look up the parameter name in __init__'s args
        const jsont &rhs = json_member(s, "value");
        std::string rhs_name;
        if(is_node_type(rhs, "Name"))
          rhs_name = json_string(json_member(rhs, "id"));

        typet attr_type = python_value_type(); // tagged union for untyped

        // Check if RHS is a constructor call: self.inner = Inner(v)
        if(
          is_node_type(rhs, "Call") &&
          is_node_type(json_member(rhs, "func"), "Name"))
        {
          std::string ctor_name =
            json_string(json_member(json_member(rhs, "func"), "id"));
          if(class_types.count(ctor_name))
            attr_type = class_types[ctor_name];
        }
        else if(!rhs_name.empty())
        {
          // Find the parameter annotation
          const jsont &init_args = json_member(*init_method, "args");
          const jsont &params = json_member(init_args, "args");
          if(params.is_array())
          {
            for(const auto &p : as_array(params))
            {
              if(json_string(json_member(p, "arg")) == rhs_name)
              {
                const jsont &ann = json_member(p, "annotation");
                if(!ann.is_null())
                  attr_type = convert_type_annotation(ann);
                // else: stays as python_value_type (tagged union)
                break;
              }
            }
          }
        }

        if(declared_fields.insert(attr_name).second)
          components.push_back(struct_typet::componentt{attr_name, attr_type});
      }
    }
  }

  // Add __class_tag as first field for dynamic dispatch
  struct_typet::componentst tagged_components;
  tagged_components.push_back(
    struct_typet::componentt{"__class_tag", signedbv_typet{32}});
  // Inherit parent class attributes
  if(bases.is_array())
  {
    for(const auto &base : as_array(bases))
    {
      if(is_node_type(base, "Name"))
      {
        std::string base_name = json_string(json_member(base, "id"));
        if(class_types.count(base_name))
        {
          const auto &parent = to_struct_type(class_types[base_name]);
          for(const auto &pc : parent.components())
          {
            if(pc.get_name() == "__class_tag")
              continue; // already added
            // Only add if not already defined in child
            bool found = false;
            for(const auto &c : components)
            {
              if(c.get_name() == pc.get_name())
              {
                found = true;
                break;
              }
            }
            if(!found)
              tagged_components.push_back(pc);
          }
        }
      }
    }
  }
  for(auto &c : components)
    tagged_components.push_back(std::move(c));

  struct_typet class_type{tagged_components};
  class_type.set_tag("python_class_" + class_name);
  class_types[class_name] = class_type;
  if(!class_tag_ids.count(class_name))
    class_tag_ids[class_name] = static_cast<int>(class_tag_ids.size()) + 1;

  // Record base classes for isinstance checks
  if(bases.is_array())
  {
    for(const auto &base : as_array(bases))
    {
      if(is_node_type(base, "Name"))
        class_bases[class_name].push_back(json_string(json_member(base, "id")));
    }
  }

  // PLR §3.3.2.1: compute C3 linearization MRO for this class.
  // MRO(C) = [C] + merge(MRO(B1), MRO(B2), ..., [B1, B2, ...])
  // where merge picks the head of the first list that isn't in
  // the tail of any other list; repeat until all lists empty.
  //
  // Idempotent: convert_class_def is invoked multiple times
  // across the passes (pre-register, full register, method
  // body convert); compute MRO exactly once per class to
  // avoid accumulating duplicates from re-runs over the
  // append-based class_bases map.
  if(class_mro.count(class_name) == 0)
  {
    // Dedup class_bases[class_name] in case previous passes
    // over the same ClassDef duplicated entries.
    auto &bv = class_bases[class_name];
    std::vector<std::string> dedup;
    std::set<std::string> seen;
    for(const auto &b : bv)
    {
      if(seen.insert(b).second)
        dedup.push_back(b);
    }
    bv = dedup;
    std::vector<std::string> mro{class_name};
    std::vector<std::vector<std::string>> seqs;
    for(const auto &b : class_bases[class_name])
    {
      auto it = class_mro.find(b);
      if(it != class_mro.end())
        seqs.push_back(it->second);
      else
        seqs.push_back({b}); // unknown base — assume just itself
    }
    if(!class_bases[class_name].empty())
      seqs.push_back(class_bases[class_name]);
    // Merge
    bool progress = true;
    while(progress)
    {
      progress = false;
      // Strip any now-empty lists.
      seqs.erase(
        std::remove_if(
          seqs.begin(),
          seqs.end(),
          [](const std::vector<std::string> &v) { return v.empty(); }),
        seqs.end());
      if(seqs.empty())
        break;
      for(std::size_t i = 0; i < seqs.size(); ++i)
      {
        const std::string &head = seqs[i].front();
        // head must not appear in the tail of any other list.
        bool in_tail = false;
        for(std::size_t j = 0; j < seqs.size() && !in_tail; ++j)
        {
          if(i == j)
            continue;
          for(std::size_t k = 1; k < seqs[j].size(); ++k)
          {
            if(seqs[j][k] == head)
            {
              in_tail = true;
              break;
            }
          }
        }
        if(!in_tail)
        {
          // Copy head by value — the following erase loop may
          // invalidate seqs[i]'s iterators/references, making
          // the `head` reference dangling and causing subsequent
          // seqs[j].front() != head mis-comparisons.
          std::string head_val = head;
          mro.push_back(head_val);
          // Remove head from the front of every sequence.
          for(auto &s : seqs)
          {
            if(!s.empty() && s.front() == head_val)
              s.erase(s.begin());
          }
          progress = true;
          break;
        }
      }
    }
    class_mro[class_name] = std::move(mro);
  }

  // Register the class type in the symbol table
  irep_idt type_symbol_id{"python::class::" + class_name};
  if(symbol_table.lookup(type_symbol_id) == nullptr)
  {
    symbolt type_sym{type_symbol_id, class_type, "python"};
    type_sym.base_name = class_name;
    type_sym.is_type = true;
    type_sym.location = loc;
    symbol_table.add(type_sym);
  }

  // Create a class object symbol for ClassName.attr access
  irep_idt class_obj_id{"python::" + class_name};
  if(symbol_table.lookup(class_obj_id) == nullptr)
  {
    symbolt class_obj{class_obj_id, class_type, "python"};
    class_obj.base_name = class_name;
    class_obj.is_lvalue = true;
    class_obj.is_state_var = true;
    class_obj.is_static_lifetime = true;
    // Initialize with class-level attribute values
    exprt::operandst field_values;
    for(const auto &comp : class_type.components())
    {
      // Set __class_tag to this class's unique ID
      if(id2string(comp.get_name()) == "__class_tag")
      {
        field_values.push_back(
          from_integer(class_tag_ids[class_name], signedbv_typet{32}));
        continue;
      }
      exprt val = safe_zero(comp.type());
      // Look for the value in the class body
      if(body.is_array())
      {
        for(const auto &item : as_array(body))
        {
          if(is_node_type(item, "AnnAssign"))
          {
            const jsont &tgt = json_member(item, "target");
            if(
              is_node_type(tgt, "Name") &&
              json_string(json_member(tgt, "id")) == id2string(comp.get_name()))
            {
              const jsont &v = json_member(item, "value");
              if(!v.is_null())
                val = safe_typecast(convert_expression(v), comp.type());
            }
          }
          else if(is_node_type(item, "Assign"))
          {
            const jsont &targets = json_member(item, "targets");
            if(targets.is_array())
            {
              for(const auto &tgt : as_array(targets))
              {
                if(
                  is_node_type(tgt, "Name") &&
                  json_string(json_member(tgt, "id")) ==
                    id2string(comp.get_name()))
                {
                  exprt rv = convert_expression(json_member(item, "value"));
                  val = safe_typecast(rv, comp.type());
                }
              }
            }
          }
        }
      }
      // If still zero and attribute is inherited, copy from parent
      if(val == safe_zero(comp.type()) && class_bases.count(class_name))
      {
        for(const auto &base_name : class_bases[class_name])
        {
          irep_idt parent_id{"python::" + base_name};
          const symbolt *parent_sym = symbol_table.lookup(parent_id);
          if(parent_sym != nullptr && parent_sym->value.id() == ID_struct)
          {
            const auto &parent_st = to_struct_type(parent_sym->value.type());
            if(parent_st.has_component(comp.get_name()))
            {
              auto idx = parent_st.component_number(comp.get_name());
              if(idx < parent_sym->value.operands().size())
              {
                val = parent_sym->value.operands()[idx];
                if(val.type() != comp.type())
                  val = safe_typecast(val, comp.type());
              }
            }
          }
        }
      }
      field_values.push_back(val);
    }
    class_obj.value = struct_exprt{std::move(field_values), class_type};
    symbol_table.add(class_obj);
  }

  // Generate class object initialization in the module body
  const symbolt *cls_sym = symbol_table.lookup(class_obj_id);
  if(cls_sym != nullptr && !cls_sym->value.is_nil())
  {
    // This will be included in the module body via convert_module_body
    // since class defs are processed in the first pass but the init
    // needs to run at module load time
  }

  // Pre-pass: register the names of every method declared in
  // the class body, so that during method-body conversion a
  // forward-reference call (e.g. self.helper() inside __init__,
  // where helper appears later in the class body) is recognised
  // as a real method rather than a missing-method bug.
  if(body.is_array())
  {
    for(const auto &item : as_array(body))
    {
      if(
        is_node_type(item, "FunctionDef") ||
        is_node_type(item, "AsyncFunctionDef"))
      {
        class_declared_methods[class_name].insert(
          json_string(json_member(item, "name")));
      }
      // Inner classes also act as 'attributes' callable from
      // self.<Inner>(...). Stubs commonly have
      //   self.exceptions = self._Exceptions()
      // where _Exceptions is a nested ClassDef. Without this
      // entry, the constructor-call falls into the missing-
      // method path and emits a spurious attribute-error.
      else if(is_node_type(item, "ClassDef"))
      {
        class_declared_methods[class_name].insert(
          json_string(json_member(item, "name")));
      }
    }
    // Also include methods inherited from base classes —
    // a subclass calling self.parent_method() shouldn't
    // trip the missing-method detector when the method is
    // defined on a base.
    auto mro_it = class_mro.find(class_name);
    if(mro_it != class_mro.end())
    {
      for(const auto &base : mro_it->second)
      {
        if(base == class_name)
          continue;
        auto bi = class_declared_methods.find(base);
        if(bi != class_declared_methods.end())
          for(const auto &m : bi->second)
            class_declared_methods[class_name].insert(m);
      }
    }
  }

  // Now convert all methods
  if(body.is_array())
  {
    std::string saved_class = current_class;
    current_class = class_name;

    for(const auto &item : as_array(body))
    {
      if((is_node_type(item, "FunctionDef") ||
          is_node_type(item, "AsyncFunctionDef")))
      {
        std::string method_name = json_string(json_member(item, "name"));

        // PLR §8.7: Check for @classmethod/@staticmethod decorator
        bool is_classmethod = false;
        bool is_staticmethod = false;
        // Phase 7 of the icontract integration plan: collect
        // per-method @require / @ensure / @snapshot decorators
        // so class methods get the same treatment as top-level
        // functions. Phase 6 already handles class @invariant
        // separately; per-method contracts compose with the
        // class invariants below.
        std::vector<const jsont *> method_require_lambdas;
        std::vector<const jsont *> method_ensure_lambdas;
        std::vector<std::pair<std::string, const jsont *>> method_snapshots;
        const jsont &decorators = json_member(item, "decorator_list");
        if(decorators.is_array())
        {
          for(const auto &dec : as_array(decorators))
          {
            if(is_node_type(dec, "Name"))
            {
              std::string dname = json_string(json_member(dec, "id"));
              if(dname == "classmethod")
                is_classmethod = true;
              if(dname == "staticmethod")
                is_staticmethod = true;
              if(dname == "property")
              {
                // Record this method as a property of the class.
                std::string m = json_string(json_member(item, "name"));
                class_property_methods[class_name].insert(m);
              }
            }
            // icontract decorators (Call form):
            //   @icontract.X(lambda ...) or @X(lambda ...)
            if(is_node_type(dec, "Call"))
            {
              const jsont &dec_func = json_member(dec, "func");
              bool is_require = false;
              bool is_ensure = false;
              bool is_snapshot = false;
              if(is_node_type(dec_func, "Attribute"))
              {
                const jsont &v = json_member(dec_func, "value");
                if(
                  is_node_type(v, "Name") &&
                  json_string(json_member(v, "id")) == "icontract")
                {
                  std::string attr = json_string(json_member(dec_func, "attr"));
                  if(attr == "require")
                    is_require = true;
                  else if(attr == "ensure")
                    is_ensure = true;
                  else if(attr == "snapshot")
                    is_snapshot = true;
                }
              }
              else if(is_node_type(dec_func, "Name"))
              {
                std::string nm = json_string(json_member(dec_func, "id"));
                if(nm == "require")
                  is_require = true;
                else if(nm == "ensure")
                  is_ensure = true;
                else if(nm == "snapshot")
                  is_snapshot = true;
              }
              if(!is_require && !is_ensure && !is_snapshot)
                continue;
              const jsont &dec_args = json_member(dec, "args");
              if(!dec_args.is_array() || as_array(dec_args).empty())
                continue;
              const jsont &first = *as_array(dec_args).begin();
              if(!is_node_type(first, "Lambda"))
                continue;
              if(is_require)
                method_require_lambdas.push_back(&first);
              else if(is_ensure)
                method_ensure_lambdas.push_back(&first);
              else
              {
                std::string snap_name;
                const jsont &dec_kws = json_member(dec, "keywords");
                if(dec_kws.is_array())
                {
                  for(const auto &kw : as_array(dec_kws))
                  {
                    if(json_string(json_member(kw, "arg")) != "name")
                      continue;
                    const jsont &val = json_member(kw, "value");
                    if(is_node_type(val, "Constant"))
                    {
                      const jsont &v = json_member(val, "value");
                      if(v.is_string())
                        snap_name = v.value;
                    }
                  }
                }
                if(!snap_name.empty())
                  method_snapshots.emplace_back(std::move(snap_name), &first);
              }
            }
          }
        }
        // Phase 7 (Liskov inheritance composition): record this
        // method's own contracts so subclasses overriding the
        // method can compose with them.
        if(!method_require_lambdas.empty())
          class_method_require_lambdas[class_name][method_name] =
            method_require_lambdas;
        if(!method_ensure_lambdas.empty())
          class_method_ensure_lambdas[class_name][method_name] =
            method_ensure_lambdas;

        // Build method with class-qualified name
        const jsont &args_node = json_member(item, "args");
        const jsont &params = json_member(args_node, "args");

        code_typet::parameterst parameters;

        // Helper: add a (pos-only or regular) parameter to this method.
        auto add_method_param = [&](const jsont &param)
        {
          std::string param_name = json_string(json_member(param, "arg"));
          // For @staticmethod, the first parameter is an ordinary
          // parameter, not 'self' — we still bind it.
          // For @classmethod, bind 'cls' so the body can reference it.
          // (Previously we skipped it, which caused 'Unknown variable:
          // cls' whenever the body actually used it.)
          typet param_type;
          if(param_name == "self" && !is_staticmethod)
            param_type = pointer_typet{class_type, config.ansi_c.pointer_width};
          else if(param_name == "cls" && is_classmethod)
            param_type = pointer_typet{class_type, config.ansi_c.pointer_width};
          else
          {
            const jsont &annotation = json_member(param, "annotation");
            param_type = annotation.is_null()
                           ? python_value_type()
                           : convert_type_annotation(annotation);
          }

          code_typet::parametert p{param_type};
          p.set_identifier(
            "python::" + class_name + "::" + method_name + "::" + param_name);
          p.set_base_name(param_name);
          parameters.push_back(p);
        };

        // PLR §8.7: positional-only parameters.
        const jsont &posonlyargs_m = json_member(args_node, "posonlyargs");
        if(posonlyargs_m.is_array())
        {
          for(const auto &param : as_array(posonlyargs_m))
            add_method_param(param);
        }
        if(params.is_array())
        {
          for(const auto &param : as_array(params))
            add_method_param(param);
        }

        // *args for class methods
        const jsont &vararg_m = json_member(args_node, "vararg");
        if(!vararg_m.is_null())
        {
          std::string va_name = json_string(json_member(vararg_m, "arg"));
          typet va_type = python_list_type(python_value_type());
          code_typet::parametert p{va_type};
          p.set_identifier(
            "python::" + class_name + "::" + method_name + "::" + va_name);
          p.set_base_name(va_name);
          parameters.push_back(p);
        }

        // kwonlyargs and **kwargs for class methods
        const jsont &kwonlyargs_m = json_member(args_node, "kwonlyargs");
        if(kwonlyargs_m.is_array())
        {
          for(const auto &param : as_array(kwonlyargs_m))
          {
            std::string pn = json_string(json_member(param, "arg"));
            const jsont &ann = json_member(param, "annotation");
            typet pt =
              ann.is_null() ? python_int_type() : convert_type_annotation(ann);
            code_typet::parametert p{pt};
            p.set_identifier(
              "python::" + class_name + "::" + method_name + "::" + pn);
            p.set_base_name(pn);
            parameters.push_back(p);
          }
        }
        const jsont &kwarg_m = json_member(args_node, "kwarg");
        if(!kwarg_m.is_null())
        {
          std::string kw_name = json_string(json_member(kwarg_m, "arg"));
          typet kw_type =
            python_dict_type(python_string_type(), python_value_type());
          code_typet::parametert p{kw_type};
          p.set_identifier(
            "python::" + class_name + "::" + method_name + "::" + kw_name);
          p.set_base_name(kw_name);
          parameters.push_back(p);
        }

        const jsont &returns = json_member(item, "returns");
        typet return_type = returns.is_null()
                              ? python_int_type()
                              : convert_type_annotation(returns);

        // NoneType → void
        if(
          !returns.is_null() &&
          json_string(json_member(returns, "id")) == "None")
          return_type = empty_typet{};

        // __init__ always returns void (it modifies self through pointer)
        if(method_name == "__init__")
          return_type = empty_typet{};

        code_typet func_type{parameters, return_type};
        irep_idt func_id{"python::" + class_name + "::" + method_name};

        if(symbol_table.lookup(func_id) == nullptr)
        {
          symbolt func_sym{func_id, func_type, "python"};
          func_sym.base_name = method_name;
          func_sym.location = get_location(item);
          func_sym.is_lvalue = true;
          symbol_table.add(func_sym);
        }
        else
        {
          // Idempotent re-call (sub-pass 1a-bis): the existing
          // symbol may have been registered with a placeholder
          // return type (e.g. when 'Bar' was only known as a
          // pre-registered placeholder during the first 1a
          // pass). Update the type now that all classes have
          // been fully registered.
          symbol_table.get_writeable_ref(func_id).type = func_type;
        }

        // Create parameter symbols
        for(const auto &p : parameters)
        {
          if(symbol_table.lookup(p.get_identifier()) == nullptr)
          {
            symbolt param_sym{p.get_identifier(), p.type(), "python"};
            param_sym.base_name = p.get_base_name();
            param_sym.location = loc;
            param_sym.is_lvalue = true;
            param_sym.is_state_var = true;
            param_sym.is_parameter = true;
            symbol_table.add(param_sym);
          }
          else
          {
            // Idempotent re-call: refresh type if it changed
            // (placeholder class -> full class struct).
            symbol_table.get_writeable_ref(p.get_identifier()).type = p.type();
          }
        }

        // Convert method body
        {
          std::string saved_func = current_function;
          if(!current_function.empty())
            enclosing_functions.push_back(current_function);
          current_function = class_name + "::" + method_name;

          code_blockt method_body;

          // Phase 6 of the icontract integration plan:
          // translate each class-level @icontract.invariant
          // lambda in the method's scope (so Name("self")
          // resolves to the method's self parameter), then
          // (a) prepend the assertions at method entry — but
          // skip __init__ since the object's fields haven't
          // been initialised yet, and (b) collect the
          // assertions for the post-walk that fires before
          // every return + at the end of method_body for
          // fall-through. Skipped for staticmethod/classmethod
          // since they don't operate on a self instance.
          std::vector<exprt> invariant_assertions;
          if(
            !icontract_invariant_lambdas.empty() && !is_staticmethod &&
            !is_classmethod)
          {
            for(const jsont *lam : icontract_invariant_lambdas)
            {
              const jsont &lam_body = json_member(*lam, "body");
              exprt cond;
              try
              {
                cond = convert_expression(lam_body);
              }
              catch(...)
              {
                cond = nil_exprt{};
              }
              if(cond.is_nil() || cond.type().id() == ID_empty)
                continue;
              if(cond.type().id() != ID_bool)
                cond = typecast_exprt{cond, bool_typet{}};
              invariant_assertions.push_back(cond);
            }
            if(method_name != "__init__")
            {
              for(const exprt &cond : invariant_assertions)
                method_body.add(code_assertt{cond});
            }
          }

          // Phase 7 of the icontract integration plan: translate
          // per-method @require / @snapshot / @ensure lambdas
          // (collected in the decorator inspection above) within
          // the method's scope so Name lookups resolve to method
          // parameters and self. require lambdas are emitted as
          // entry assumes; snapshots are emitted as entry-state
          // captures stored in synthesised __icontract_old_<name>
          // symbols and exposed via active_old_snapshots; ensure
          // lambdas are translated into a per-method
          // method_ensure_assertions vector that the
          // post-process walk further down injects before each
          // return + at the implicit fall-through.
          //
          // Phase 7 (Liskov inheritance composition): collect
          // inherited contracts from direct base classes here
          // FIRST so the result-symbol decision below knows
          // whether ANY ensures (own or inherited) will be
          // emitted. Walks direct bases left-to-right and
          // looks up class_method_*_lambdas for the same
          // method name.
          //
          // Walks the C3 MRO (skipping the class itself) so
          // grandparent and farther-ancestor contracts are
          // included transitively. Each ancestor's contracts
          // are added in MRO order; the OR/AND composition
          // below treats the union as a single weakening
          // (preconditions) or strengthening (postconditions)
          // step against the child's own.
          std::vector<const jsont *> inherited_requires;
          std::vector<const jsont *> inherited_ensures;
          {
            auto mro_lookup = class_mro.find(class_name);
            if(mro_lookup != class_mro.end())
            {
              for(std::size_t mi = 1; mi < mro_lookup->second.size(); ++mi)
              {
                const std::string &ancestor = mro_lookup->second[mi];
                auto rit = class_method_require_lambdas.find(ancestor);
                if(rit != class_method_require_lambdas.end())
                {
                  auto mit = rit->second.find(method_name);
                  if(mit != rit->second.end())
                    for(const jsont *lam : mit->second)
                      inherited_requires.push_back(lam);
                }
                auto eit = class_method_ensure_lambdas.find(ancestor);
                if(eit != class_method_ensure_lambdas.end())
                {
                  auto mit = eit->second.find(method_name);
                  if(mit != eit->second.end())
                    for(const jsont *lam : mit->second)
                      inherited_ensures.push_back(lam);
                }
              }
            }
          }
          irep_idt method_result_symbol_id;
          if(!method_ensure_lambdas.empty() || !inherited_ensures.empty())
          {
            typet rsym_type =
              return_type.id() == ID_empty ? python_int_type() : return_type;
            std::string rsym_name =
              "python::" + class_name + "::" + method_name + "::result";
            method_result_symbol_id = irep_idt{rsym_name};
            if(!symbol_table.has_symbol(method_result_symbol_id))
            {
              symbolt rsym{method_result_symbol_id, rsym_type, "python"};
              rsym.base_name = "result";
              rsym.is_lvalue = true;
              rsym.is_state_var = true;
              symbol_table.add(rsym);
            }
          }
          // Phase 7 (Liskov inheritance composition): if this
          // method overrides a base-class method, compose the
          // preconditions weakening (parent_pre OR child_pre)
          // and the postconditions strengthening (parent_post
          // AND child_post). Walk direct bases left-to-right
          // and look up class_method_*_lambdas for the same
          // method name.
          //
          // Translates each lambda body in the method's scope
          // (so `self` resolves to the current method's self
          // pointer; the lambda body's references to fields
          // resolve via the inherited struct layout). For
          // multiple parent bases we OR/AND across all of them
          // — common in mixin scenarios.
          //
          // This is single-level inheritance only. For
          // grandparent contracts, the parent's recorded set
          // already reflects ITS own contracts only (not its
          // own ancestors'); a follow-up commit can extend this
          // to walk transitively.
          // (inherited_requires / inherited_ensures already
          // populated above, before result-symbol creation.)
          // Helper: translate a list of lambdas into a list of
          // bool exprts in the current scope.
          auto translate_lambdas =
            [&](const std::vector<const jsont *> &lams) -> std::vector<exprt>
          {
            std::vector<exprt> out;
            for(const jsont *lam : lams)
            {
              const jsont &lb = json_member(*lam, "body");
              exprt c;
              try
              {
                c = convert_expression(lb);
              }
              catch(...)
              {
                c = nil_exprt{};
              }
              if(c.is_nil() || c.type().id() == ID_empty)
                continue;
              if(c.type().id() != ID_bool)
                c = typecast_exprt{c, bool_typet{}};
              out.push_back(c);
            }
            return out;
          };
          std::vector<exprt> own_require_exprs =
            translate_lambdas(method_require_lambdas);
          std::vector<exprt> parent_require_exprs =
            translate_lambdas(inherited_requires);
          // Compose effective precondition (Liskov weakening):
          //   if both own and parent: assume(AND(own) OR AND(parent))
          //   if only own: assume each
          //   if only parent: assume each
          if(!own_require_exprs.empty() && !parent_require_exprs.empty())
          {
            auto build_and = [&](const std::vector<exprt> &v) -> exprt
            {
              if(v.empty())
                return true_exprt{};
              if(v.size() == 1)
                return v.front();
              and_exprt::operandst ops;
              for(const exprt &e : v)
                ops.push_back(e);
              return and_exprt{ops};
            };
            exprt own_pre = build_and(own_require_exprs);
            exprt par_pre = build_and(parent_require_exprs);
            method_body.add(code_assumet{or_exprt{own_pre, par_pre}});
            if(symbol_table.has_symbol(func_id))
            {
              typet &t = symbol_table.get_writeable_ref(func_id).type;
              static_cast<exprt &>(t.add(ID_C_spec_requires))
                .operands()
                .push_back(or_exprt{own_pre, par_pre});
            }
          }
          else
          {
            for(const exprt &cond : own_require_exprs)
            {
              method_body.add(code_assumet{cond});
              if(symbol_table.has_symbol(func_id))
              {
                typet &t = symbol_table.get_writeable_ref(func_id).type;
                static_cast<exprt &>(t.add(ID_C_spec_requires))
                  .operands()
                  .push_back(cond);
              }
            }
            for(const exprt &cond : parent_require_exprs)
            {
              method_body.add(code_assumet{cond});
              if(symbol_table.has_symbol(func_id))
              {
                typet &t = symbol_table.get_writeable_ref(func_id).type;
                static_cast<exprt &>(t.add(ID_C_spec_requires))
                  .operands()
                  .push_back(cond);
              }
            }
          }
          // Translate snapshots and emit capture assignments.
          std::map<std::string, exprt> saved_method_old_snapshots;
          saved_method_old_snapshots.swap(active_old_snapshots);
          for(const auto &snap : method_snapshots)
          {
            const std::string &name = snap.first;
            const jsont &lam = *snap.second;
            const jsont &lam_body = json_member(lam, "body");
            exprt cap;
            try
            {
              cap = convert_expression(lam_body);
            }
            catch(...)
            {
              cap = nil_exprt{};
            }
            if(cap.is_nil() || cap.type().id() == ID_empty)
              continue;
            std::string old_id_str = "python::" + class_name +
                                     "::" + method_name + "::__icontract_old_" +
                                     name;
            irep_idt old_id{old_id_str};
            if(!symbol_table.has_symbol(old_id))
            {
              symbolt old_sym{old_id, cap.type(), "python"};
              old_sym.base_name = "__icontract_old_" + name;
              old_sym.is_lvalue = true;
              old_sym.is_state_var = true;
              symbol_table.add(old_sym);
            }
            const symbolt &old_sym = symbol_table.lookup_ref(old_id);
            method_body.add(code_frontend_assignt{old_sym.symbol_expr(), cap});
            active_old_snapshots[name] = old_sym.symbol_expr();
          }
          // Translate ensure lambdas (with `result` and OLD.NAME
          // bindings now in scope).
          std::vector<exprt> method_ensure_assertions;
          for(const jsont *lam : method_ensure_lambdas)
          {
            const jsont &lam_body = json_member(*lam, "body");
            exprt cond;
            try
            {
              cond = convert_expression(lam_body);
            }
            catch(...)
            {
              cond = nil_exprt{};
            }
            if(cond.is_nil() || cond.type().id() == ID_empty)
              continue;
            if(cond.type().id() != ID_bool)
              cond = typecast_exprt{cond, bool_typet{}};
            method_ensure_assertions.push_back(cond);
            if(symbol_table.has_symbol(func_id))
            {
              typet &t = symbol_table.get_writeable_ref(func_id).type;
              static_cast<exprt &>(t.add(ID_C_spec_ensures))
                .operands()
                .push_back(cond);
            }
          }
          // Phase 7 Liskov: parent's postconditions are
          // strengthened (AND-composed) with child's. We
          // simply translate inherited_ensures into the same
          // method_ensure_assertions list — the post-process
          // walk asserts each in turn, which is equivalent to
          // their conjunction.
          for(const jsont *lam : inherited_ensures)
          {
            const jsont &lam_body = json_member(*lam, "body");
            exprt cond;
            try
            {
              cond = convert_expression(lam_body);
            }
            catch(...)
            {
              cond = nil_exprt{};
            }
            if(cond.is_nil() || cond.type().id() == ID_empty)
              continue;
            if(cond.type().id() != ID_bool)
              cond = typecast_exprt{cond, bool_typet{}};
            method_ensure_assertions.push_back(cond);
            if(symbol_table.has_symbol(func_id))
            {
              typet &t = symbol_table.get_writeable_ref(func_id).type;
              static_cast<exprt &>(t.add(ID_C_spec_ensures))
                .operands()
                .push_back(cond);
            }
          }
          // Under --python-lazy-stubs AND while processing an
          // imported module, skip the method body. The stub
          // becomes a pure type surface: method signature only,
          // returns nondet. This avoids the cascaded assertion
          // failures and string-refinement-solver blow-ups
          // that boto3 / similar stubs cause when their dense
          // regex preconditions interact with nondet kwargs.
          bool skip_body = python_lazy_stubs && processing_import;

          // Auto-detect PySpec-style typed stubs: a method with
          // 'kwargs: Unpack[TypedDict]' is almost certainly a
          // generated stub whose body is precondition
          // assertions on kwargs. When the caller passes known
          // constants these are easy for the solver; when the
          // caller passes nondet values (common for imports
          // transiently loaded from stub modules), the dense
          // regex preconditions interact badly with the
          // string refinement loop. Skip such bodies when
          // processing imported modules.
          std::string unpack_td_name;
          std::string kwargs_param_name;
          if(processing_import && !skip_body)
          {
            const jsont &margs = json_member(item, "args");
            const jsont &kwarg = json_member(margs, "kwarg");
            if(!kwarg.is_null())
            {
              kwargs_param_name = json_string(json_member(kwarg, "arg"));
              const jsont &ann = json_member(kwarg, "annotation");
              if(is_node_type(ann, "Subscript"))
              {
                const jsont &av = json_member(ann, "value");
                if(is_node_type(av, "Name"))
                {
                  std::string an = json_string(json_member(av, "id"));
                  if(an == "Unpack")
                  {
                    skip_body = true;
                    // Extract TypedDict name from Unpack's slice.
                    const jsont &slc = json_member(ann, "slice");
                    if(is_node_type(slc, "Name"))
                    {
                      unpack_td_name = json_string(json_member(slc, "id"));
                      // Persist the Unpack[TypedDict] mapping
                      // for the call site to consult when
                      // emitting field-type checks on values
                      // spread via PEP 448 **kwargs.
                      method_kwargs_unpack[func_id] = unpack_td_name;
                    }
                  }
                }
              }
            }
          }
          const jsont &method_body_json = json_member(item, "body");

          // Stage 1 of the re-precision plan: walk the method
          // body looking for regex-assertion patterns of the
          // shape
          //     assert compile("...").search(kwargs[K1][K2]...) is not None
          // and record the (kwarg_path, pattern) so user call
          // sites can emit a regex-no-match property when their
          // nested-Dict kwarg value resolves to an empty string.
          if(!kwargs_param_name.empty() && method_body_json.is_array())
          {
            std::function<void(const jsont &)> scan;
            scan = [&](const jsont &node)
            {
              if(!node.is_object() && !node.is_array())
                return;
              if(node.is_array())
              {
                for(const auto &c : as_array(node))
                  scan(c);
                return;
              }
              // Identify a Subscript chain rooted at
              // Name(kwargs_param_name).
              auto extract_kwarg_path =
                [&](const jsont &n) -> std::optional<std::vector<std::string>>
              {
                std::vector<std::string> path;
                const jsont *cur = &n;
                while(is_node_type(*cur, "Subscript"))
                {
                  const jsont &s_node = json_member(*cur, "slice");
                  if(!is_node_type(s_node, "Constant"))
                    return std::nullopt;
                  const jsont &k = json_member(s_node, "value");
                  if(!k.is_string())
                    return std::nullopt;
                  path.insert(path.begin(), k.value);
                  cur = &json_member(*cur, "value");
                }
                if(
                  is_node_type(*cur, "Name") &&
                  json_string(json_member(*cur, "id")) == kwargs_param_name &&
                  !path.empty())
                  return path;
                return std::nullopt;
              };
              // Look for Call(Attribute(value=<Compile>, attr=
              //   {search,match,fullmatch}), args=[<subj>])
              if(is_node_type(node, "Call"))
              {
                const jsont &fn = json_member(node, "func");
                const jsont &cargs = json_member(node, "args");
                if(
                  is_node_type(fn, "Attribute") && cargs.is_array() &&
                  !as_array(cargs).empty())
                {
                  std::string attr = json_string(json_member(fn, "attr"));
                  if(attr == "search" || attr == "match" || attr == "fullmatch")
                  {
                    const jsont &recv = json_member(fn, "value");
                    // Pattern recovery: receiver is Call to
                    // compile(LITERAL) [optionally re.compile].
                    std::optional<std::string> pat;
                    if(is_node_type(recv, "Call"))
                    {
                      const jsont &cf = json_member(recv, "func");
                      bool is_compile = false;
                      if(
                        is_node_type(cf, "Name") &&
                        json_string(json_member(cf, "id")) == "compile")
                        is_compile = true;
                      else if(
                        is_node_type(cf, "Attribute") &&
                        json_string(json_member(cf, "attr")) == "compile")
                      {
                        const jsont &cv = json_member(cf, "value");
                        if(
                          is_node_type(cv, "Name") &&
                          json_string(json_member(cv, "id")) == "re")
                          is_compile = true;
                      }
                      if(is_compile)
                      {
                        const jsont &cca = json_member(recv, "args");
                        if(cca.is_array() && !as_array(cca).empty())
                        {
                          const jsont &p0 = *as_array(cca).begin();
                          if(is_node_type(p0, "Constant"))
                          {
                            const jsont &v = json_member(p0, "value");
                            if(v.is_string())
                              pat = v.value;
                          }
                        }
                      }
                    }
                    if(pat.has_value())
                    {
                      const jsont &subj = *as_array(cargs).begin();
                      auto path = extract_kwarg_path(subj);
                      if(path.has_value())
                      {
                        method_regex_asserts[func_id].push_back(
                          stub_regex_assertt{
                            std::move(*path), std::move(*pat)});
                      }
                    }
                  }
                }
              }
              // Recurse into all sub-objects.
              if(node.is_object())
              {
                const auto &obj = static_cast<const json_objectt &>(node);
                for(const auto &kv : obj)
                  scan(kv.second);
              }
            };
            for(const auto &s : as_array(method_body_json))
              scan(s);
          }

          if(!skip_body && method_body_json.is_array())
          {
            for(const auto &s : as_array(method_body_json))
              method_body.add(convert_statement(s));
          }

          // Phase 6 of the icontract integration plan: inject
          // invariant assertions before every return in the
          // method body, plus a final assertion at the end so
          // fall-through paths see it too. Phase 7 unifies the
          // class invariant + per-method ensure assertions into
          // a single walk: invariants come first (entry-vs-exit
          // semantics for the class), then ensures (postcondition
          // semantics for the method). When the method has
          // ensures clauses, we also bind `result` to the return
          // value before asserting so postconditions can
          // reference it.
          std::vector<exprt> exit_assertions = invariant_assertions;
          for(const exprt &cond : method_ensure_assertions)
            exit_assertions.push_back(cond);
          if(!exit_assertions.empty())
          {
            std::function<void(codet &)> inject_at_returns =
              [&](codet &c) -> void
            {
              for(auto &op : c.operands())
              {
                if(op.id() != ID_code)
                  continue;
                codet &inner = static_cast<codet &>(op);
                if(inner.get_statement() == ID_return)
                {
                  const code_frontend_returnt &ret =
                    static_cast<const code_frontend_returnt &>(inner);
                  code_blockt blk;
                  // Bind result symbol if there are ensures and
                  // the return carries a value.
                  if(
                    !method_ensure_assertions.empty() &&
                    ret.has_return_value() &&
                    !method_result_symbol_id.empty() &&
                    symbol_table.has_symbol(method_result_symbol_id))
                  {
                    const symbolt &rsym =
                      symbol_table.lookup_ref(method_result_symbol_id);
                    exprt rv = ret.return_value();
                    if(rv.type() != rsym.type)
                      rv = safe_typecast(rv, rsym.type);
                    blk.add(code_frontend_assignt{rsym.symbol_expr(), rv});
                  }
                  for(const exprt &cond : exit_assertions)
                    blk.add(code_assertt{cond});
                  blk.add(static_cast<const codet &>(inner));
                  op = std::move(blk);
                }
                else
                {
                  inject_at_returns(inner);
                }
              }
            };
            inject_at_returns(method_body);
            // Fall-through: append the assertions at the end. If
            // the last statement was a return, this is
            // unreachable (no harm). Otherwise it covers the
            // implicit return case.
            for(const exprt &cond : exit_assertions)
              method_body.add(code_assertt{cond});
          }
          // Restore the parent's @snapshot scope.
          active_old_snapshots.swap(saved_method_old_snapshots);

          // Tier 1B: selective precondition processing. When we
          // skip the stub body due to Unpack detection, emit
          // key-presence checks for each Required field of the
          // TypedDict. This catches 'missing required argument'
          // bugs without triggering the regex/length assertions
          // that overwhelm the string refinement solver.
          //
          // Opt-in via --python-required-kwarg-checks: callers
          // that use **dict spread don't currently populate
          // kwargs reliably, which produces FPs for spread-based
          // callers.
          if(
            skip_body && python_required_kwarg_checks &&
            !unpack_td_name.empty() && !kwargs_param_name.empty())
          {
            auto ri = typed_dict_required.find(unpack_td_name);
            if(ri != typed_dict_required.end() && !ri->second.empty())
            {
              // Resolve the kwargs parameter symbol (already
              // registered as python::<ClassName>::<method>::<name>).
              std::string kwargs_sym =
                "python::" + current_function + "::" + kwargs_param_name;
              const symbolt *ks = symbol_table.lookup(irep_idt{kwargs_sym});
              if(ks != nullptr && is_python_dict_type(ks->type))
              {
                const auto &dict_st = to_struct_type(ks->type);
                const auto &keys_type =
                  to_array_type(dict_st.components()[1].type());
                member_exprt length{
                  ks->symbol_expr(), "length", signedbv_typet{64}};
                member_exprt keys_arr{ks->symbol_expr(), "keys", keys_type};
                for(const auto &req_key : ri->second)
                {
                  // Build 'assert exists i. 0<=i<length &&
                  // keys[i] == req_key'. Same formula shape
                  // as the dict subscript's KeyError check.
                  exprt typed_req_key = python_string_literal(req_key);
                  if(typed_req_key.type() != keys_type.element_type())
                    typed_req_key =
                      safe_typecast(typed_req_key, keys_type.element_type());
                  exprt found = false_exprt{};
                  for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
                  {
                    exprt idx = from_integer(i, signedbv_typet{64});
                    exprt in_range = binary_relation_exprt{idx, ID_lt, length};
                    exprt match =
                      equal_exprt{index_exprt{keys_arr, idx}, typed_req_key};
                    found = or_exprt{found, and_exprt{in_range, match}};
                  }
                  code_assertt req_assert{found};
                  source_locationt rloc;
                  rloc.set_file(filename);
                  rloc.set_property_class("required-kwarg");
                  rloc.set_comment(
                    "missing required keyword argument '" + req_key + "'");
                  req_assert.add_source_location() = rloc;
                  method_body.add(std::move(req_assert));
                }
              }
            }
          }

          current_function = saved_func;
          if(
            !enclosing_functions.empty() &&
            enclosing_functions.back() == saved_func)
            enclosing_functions.pop_back();

          symbolt *sym_ptr = symbol_table.get_writeable(func_id);
          if(sym_ptr != nullptr)
            sym_ptr->value = method_body;
        }
      }
    }

    // Phase 7 (icontract): when this class has invariants and
    // inherits methods that aren't overridden here, synthesise
    // wrapper methods that delegate to the parent's body but
    // assert this class's invariants at entry and exit. Without
    // wrappers, `child.parent_method()` reaches the parent body
    // via the MRO dispatch but the child's own invariants never
    // fire — a soundness gap when the child's invariant
    // constrains a field the parent's method mutates.
    //
    // For __init__ specifically we skip the entry assertion
    // (the object's fields haven't been initialised yet) but
    // still emit the exit assertion so the child sees the
    // post-construction state.
    //
    // The synthesised wrapper has the same signature as the
    // parent's method, but `self` becomes a pointer to the
    // child's struct type. The body casts self to the parent
    // type before the inherited call.
    if(!icontract_invariant_lambdas.empty())
    {
      auto mro_it = class_mro.find(class_name);
      if(mro_it != class_mro.end() && class_types.count(class_name) > 0)
      {
        const struct_typet &child_struct = class_types[class_name];
        // class_declared_methods[class_name] also includes
        // inherited names (populated by the missing-method
        // detector). For wrapper synthesis we need names
        // declared DIRECTLY on this class, so walk the body to
        // compute the own set.
        std::set<std::string> own_methods;
        for(const auto &item : as_array(body))
        {
          if(
            is_node_type(item, "FunctionDef") ||
            is_node_type(item, "AsyncFunctionDef"))
            own_methods.insert(json_string(json_member(item, "name")));
        }
        std::set<std::string> already_wrapped;
        for(std::size_t mi = 1; mi < mro_it->second.size(); ++mi)
        {
          const std::string &ancestor = mro_it->second[mi];
          auto adm_it = class_declared_methods.find(ancestor);
          if(adm_it == class_declared_methods.end())
            continue;
          for(const std::string &mname : adm_it->second)
          {
            if(own_methods.count(mname))
              continue;
            if(already_wrapped.count(mname))
              continue;
            already_wrapped.insert(mname);
            // staticmethod / property / classmethod methods need
            // bespoke handling — skip wrapping for now.
            if(class_property_methods[ancestor].count(mname))
              continue;
            irep_idt parent_method_id{"python::" + ancestor + "::" + mname};
            const symbolt *parent_sym = symbol_table.lookup(parent_method_id);
            if(parent_sym == nullptr || parent_sym->type.id() != ID_code)
              continue;
            const code_typet &parent_ct = to_code_type(parent_sym->type);
            const code_typet::parameterst &parent_params =
              parent_ct.parameters();
            if(parent_params.empty())
              continue; // no self -> not an instance method
            const typet &parent_self_t = parent_params[0].type();
            if(parent_self_t.id() != ID_pointer)
              continue;
            // Build wrapper signature with self as ptr-to-child.
            code_typet::parameterst wrapper_params;
            std::vector<symbol_exprt> wrapper_param_exprs;
            std::string wrapper_qfn = class_name + "::" + mname;
            // self comes first.
            {
              typet child_self_ptr =
                pointer_typet{child_struct, config.ansi_c.pointer_width};
              code_typet::parametert p{child_self_ptr};
              std::string id_str = "python::" + wrapper_qfn + "::self";
              p.set_identifier(id_str);
              p.set_base_name("self");
              wrapper_params.push_back(p);
              irep_idt sid{id_str};
              if(!symbol_table.has_symbol(sid))
              {
                symbolt s{sid, child_self_ptr, "python"};
                s.base_name = "self";
                s.location = loc;
                s.is_lvalue = true;
                s.is_state_var = true;
                s.is_parameter = true;
                symbol_table.add(s);
              }
              wrapper_param_exprs.push_back(
                symbol_table.lookup_ref(sid).symbol_expr());
            }
            // Remaining params: copy from parent's signature with
            // wrapper-scoped identifiers.
            for(std::size_t pi = 1; pi < parent_params.size(); ++pi)
            {
              const auto &pp = parent_params[pi];
              std::string pname = id2string(pp.get_base_name());
              code_typet::parametert wp{pp.type()};
              std::string id_str = "python::" + wrapper_qfn + "::" + pname;
              wp.set_identifier(id_str);
              wp.set_base_name(pname);
              wrapper_params.push_back(wp);
              irep_idt sid{id_str};
              if(!symbol_table.has_symbol(sid))
              {
                symbolt s{sid, pp.type(), "python"};
                s.base_name = pname;
                s.location = loc;
                s.is_lvalue = true;
                s.is_state_var = true;
                s.is_parameter = true;
                symbol_table.add(s);
              }
              wrapper_param_exprs.push_back(
                symbol_table.lookup_ref(sid).symbol_expr());
            }
            typet wrapper_return = parent_ct.return_type();
            code_typet wrapper_type{wrapper_params, wrapper_return};
            irep_idt wrapper_func_id{"python::" + wrapper_qfn};
            if(!symbol_table.has_symbol(wrapper_func_id))
            {
              symbolt fsym{wrapper_func_id, wrapper_type, "python"};
              fsym.base_name = mname;
              fsym.location = loc;
              fsym.is_lvalue = true;
              symbol_table.add(fsym);
            }
            else
            {
              symbol_table.get_writeable_ref(wrapper_func_id).type =
                wrapper_type;
            }
            // Translate child invariants in the wrapper's scope
            // so Name(self) resolves to the wrapper's self
            // parameter.
            std::string saved_cf = current_function;
            current_function = wrapper_qfn;
            std::vector<exprt> wrapper_invariants;
            for(const jsont *lam : icontract_invariant_lambdas)
            {
              const jsont &lam_body = json_member(*lam, "body");
              exprt cond;
              try
              {
                cond = convert_expression(lam_body);
              }
              catch(...)
              {
                cond = nil_exprt{};
              }
              if(cond.is_nil() || cond.type().id() == ID_empty)
                continue;
              if(cond.type().id() != ID_bool)
                cond = typecast_exprt{cond, bool_typet{}};
              wrapper_invariants.push_back(cond);
            }
            current_function = saved_cf;
            // Build wrapper body.
            code_blockt wrapper_body;
            // Entry assertions (skip for __init__).
            if(mname != "__init__")
            {
              for(const exprt &cond : wrapper_invariants)
                wrapper_body.add(code_assertt{cond});
            }
            // Build call to parent's method.
            // self is passed cast to the parent's pointer type
            // (the underlying struct shape is compatible because
            // child is a structural extension).
            exprt::operandst call_args;
            exprt self_arg = wrapper_param_exprs[0];
            if(self_arg.type() != parent_self_t)
              self_arg = typecast_exprt{self_arg, parent_self_t};
            call_args.push_back(self_arg);
            for(std::size_t pi = 1; pi < wrapper_param_exprs.size(); ++pi)
            {
              exprt arg = wrapper_param_exprs[pi];
              const typet &want = parent_params[pi].type();
              if(arg.type() != want)
                arg = typecast_exprt{arg, want};
              call_args.push_back(arg);
            }
            side_effect_expr_function_callt parent_call{
              parent_sym->symbol_expr(),
              std::move(call_args),
              wrapper_return,
              loc};
            if(wrapper_return.id() == ID_empty)
            {
              wrapper_body.add(code_expressiont{std::move(parent_call)});
              for(const exprt &cond : wrapper_invariants)
                wrapper_body.add(code_assertt{cond});
            }
            else
            {
              // Capture the return value in a temp so we can
              // assert invariants AFTER the body's effects but
              // BEFORE returning. The temp is per-class-method
              // so it's stable across re-conversion.
              std::string tmp_name =
                "__icontract_wrap_ret_" + class_name + "_" + mname;
              std::string tmp_qname = "python::" + tmp_name;
              irep_idt tmp_id{tmp_qname};
              if(!symbol_table.has_symbol(tmp_id))
              {
                symbolt tmp_sym{tmp_id, wrapper_return, "python"};
                tmp_sym.base_name = tmp_name;
                tmp_sym.is_lvalue = true;
                tmp_sym.is_state_var = true;
                symbol_table.add(tmp_sym);
              }
              const symbolt &tmp_sym = symbol_table.lookup_ref(tmp_id);
              wrapper_body.add(code_frontend_assignt{
                tmp_sym.symbol_expr(), std::move(parent_call)});
              for(const exprt &cond : wrapper_invariants)
                wrapper_body.add(code_assertt{cond});
              wrapper_body.add(code_frontend_returnt{tmp_sym.symbol_expr()});
            }
            symbol_table.get_writeable_ref(wrapper_func_id).value =
              wrapper_body;
            // Record the synthesised wrapper so future
            // reconversion / dispatch sees the method.
            class_declared_methods[class_name].insert(mname);
          }
        }
      }
    }

    current_class = saved_class;
  }

  return code_skipt{};
}

codet python_convertert::convert_expr_stmt(const jsont &stmt)
{
  // Expression statement (e.g., function call as statement)
  const jsont &value = json_member(stmt, "value");

  // PLR §6.2.9: yield X → __gen_result.append(X) (eager evaluation)
  if(
    is_node_type(value, "Yield") && !current_function.empty() &&
    generator_functions.count(current_function))
  {
    const jsont &yield_val = json_member(value, "value");
    exprt val = yield_val.is_null() ? from_integer(0, python_int_type())
                                    : convert_expression(yield_val);

    std::string grn = "__gen_result_" + current_function;
    std::string grq = qualify_name(grn);
    irep_idt gri{grq};
    const symbolt *grs = symbol_table.lookup(gri);
    if(grs != nullptr)
    {
      const auto &list_st = to_struct_type(grs->type);
      const auto &data_type = to_array_type(list_st.components()[1].type());
      member_exprt data{grs->symbol_expr(), "data", data_type};
      member_exprt length{grs->symbol_expr(), "length", signedbv_typet{64}};
      code_blockt block;
      // data[length] = val
      exprt typed_val = val;
      if(typed_val.type() != data_type.element_type())
        typed_val = safe_typecast(typed_val, data_type.element_type());
      block.add(code_frontend_assignt{index_exprt{data, length}, typed_val});
      // length += 1
      block.add(code_frontend_assignt{
        length, plus_exprt{length, from_integer(1, signedbv_typet{64})}});
      return std::move(block);
    }
  }
  // PLR §6.2.9: 'yield from G()' delegates: each value yielded
  // by G is yielded by the enclosing generator. Equivalent to:
  //     for v in G():
  //         yield v
  // We model this by evaluating G() (which produces a list under
  // our eager generator model) and appending each element to
  // __gen_result.
  if(
    is_node_type(value, "YieldFrom") && !current_function.empty() &&
    generator_functions.count(current_function))
  {
    const jsont &yf_val = json_member(value, "value");
    exprt src = convert_expression(yf_val);
    std::string grn = "__gen_result_" + current_function;
    std::string grq = qualify_name(grn);
    irep_idt gri{grq};
    const symbolt *grs = symbol_table.lookup(gri);
    if(
      !src.is_nil() && is_python_list_type(src.type()) && grs != nullptr)
    {
      const auto &dst_list_st = to_struct_type(grs->type);
      const auto &dst_data_type =
        to_array_type(dst_list_st.components()[1].type());
      member_exprt dst_data{grs->symbol_expr(), "data", dst_data_type};
      member_exprt dst_len{grs->symbol_expr(), "length", signedbv_typet{64}};
      const auto &src_list_st = to_struct_type(src.type());
      const auto &src_data_type =
        to_array_type(src_list_st.components()[1].type());
      member_exprt src_data{src, "data", src_data_type};
      member_exprt src_len{src, "length", signedbv_typet{64}};
      code_blockt block;
      // For each i in [0, MAX), if i < src_len, append src.data[i].
      for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
      {
        exprt idx = from_integer(i, signedbv_typet{64});
        exprt elem = index_exprt{src_data, idx};
        if(elem.type() != dst_data_type.element_type())
          elem = safe_typecast(elem, dst_data_type.element_type());
        code_blockt append;
        append.add(
          code_frontend_assignt{index_exprt{dst_data, dst_len}, elem});
        append.add(code_frontend_assignt{
          dst_len, plus_exprt{dst_len, from_integer(1, signedbv_typet{64})}});
        block.add(code_ifthenelset{
          binary_relation_exprt{idx, ID_lt, src_len}, std::move(append)});
      }
      return std::move(block);
    }
  }

  // Check for __CPROVER_assume calls
  if(is_node_type(value, "Call"))
  {
    const jsont &func = json_member(value, "func");
    std::string func_name;
    if(is_node_type(func, "Name"))
      func_name = json_string(json_member(func, "id"));

    if(func_name == "__CPROVER_assume" || func_name == "__ESBMC_assume")
    {
      const jsont &args = json_member(value, "args");
      if(args.is_array() && !as_array(args).empty())
      {
        exprt cond = convert_expression(*as_array(args).begin());
        if(cond.type() != bool_typet{})
          cond = safe_typecast(cond, bool_typet{});
        code_assumet assume{cond};
        assume.add_source_location() = get_location(stmt);
        return std::move(assume);
      }
    }

    // Handle list.append(val)
    if(is_node_type(func, "Attribute"))
    {
      std::string method = json_string(json_member(func, "attr"));
      if(method == "append")
      {
        exprt obj = convert_expression(json_member(func, "value"));
        const jsont &call_args = json_member(value, "args");
        if(
          !obj.is_nil() && is_python_list_type(obj.type()) &&
          call_args.is_array() && !as_array(call_args).empty())
        {
          exprt val = convert_expression(*as_array(call_args).begin());
          source_locationt loc = get_location(stmt);

          const auto &list_st = to_struct_type(obj.type());
          const auto &data_type = to_array_type(list_st.components()[1].type());

          // lst.data[lst.length] = val
          member_exprt length{obj, "length", python_int_type()};
          member_exprt data{obj, "data", data_type};
          index_exprt slot{data, length};

          if(val.type() != data_type.element_type())
            val = typecast_exprt{val, data_type.element_type()};

          code_blockt block;
          code_frontend_assignt store{slot, val};
          store.add_source_location() = loc;
          block.add(std::move(store));

          // lst.length += 1
          code_frontend_assignt inc_len{
            length, plus_exprt{length, from_integer(1, python_int_type())}};
          inc_len.add_source_location() = loc;
          block.add(std::move(inc_len));

          return std::move(block);
        }
      }
      else if(method == "insert")
      {
        exprt obj = convert_expression(json_member(func, "value"));
        const jsont &call_args = json_member(value, "args");
        if(
          !obj.is_nil() && is_python_list_type(obj.type()) &&
          call_args.is_array() && as_array(call_args).size() >= 2)
        {
          auto arg_it = as_array(call_args).begin();
          exprt idx_expr = convert_expression(*arg_it);
          ++arg_it;
          exprt val = convert_expression(*arg_it);
          source_locationt loc = get_location(stmt);
          const auto &list_st = to_struct_type(obj.type());
          const auto &data_type = to_array_type(list_st.components()[1].type());
          member_exprt length{obj, "length", python_int_type()};
          member_exprt data{obj, "data", data_type};
          if(idx_expr.type() != python_int_type())
            idx_expr = safe_typecast(idx_expr, python_int_type());
          if(val.type() != data_type.element_type())
            val = safe_typecast(val, data_type.element_type());
          // PLR §4.6.3: list.insert(i, x) clamps i to
          // [0, len(list)]. Index past the end appends; very
          // negative index inserts at front (idx + len <= 0 → 0,
          // else idx + len). We materialise the clamped index
          // into a temp so the shift loop and the write below
          // both reference the same value.
          // clamped =
          //   idx < 0 ? max(0, idx + length)
          //           : min(idx, length)
          exprt zero64 = from_integer(0, python_int_type());
          exprt neg_branch = if_exprt{
            binary_relation_exprt{plus_exprt{idx_expr, length}, ID_lt, zero64},
            zero64,
            plus_exprt{idx_expr, length}};
          exprt pos_branch = if_exprt{
            binary_relation_exprt{idx_expr, ID_gt, length}, length, idx_expr};
          exprt clamped = if_exprt{
            binary_relation_exprt{idx_expr, ID_lt, zero64},
            neg_branch,
            pos_branch};
          static unsigned ins_ctr = 0;
          std::string tname = "__ins_idx_" + std::to_string(ins_ctr++);
          irep_idt tid{qualify_name(tname)};
          if(symbol_table.lookup(tid) == nullptr)
          {
            symbolt ts{tid, python_int_type(), "python"};
            ts.base_name = tname;
            ts.is_lvalue = true;
            ts.is_state_var = true;
            ts.is_static_lifetime = current_function.empty();
            symbol_table.add(ts);
          }
          symbol_exprt ins_idx = symbol_table.lookup_ref(tid).symbol_expr();
          code_blockt block;
          block.add(code_frontend_assignt{ins_idx, std::move(clamped)});
          for(int i = PYTHON_MAX_LIST_LENGTH - 1; i >= 1; i--)
          {
            exprt ii = from_integer(i, python_int_type());
            exprt prev = from_integer(i - 1, python_int_type());
            block.add(code_ifthenelset{
              and_exprt{
                binary_relation_exprt{prev, ID_ge, ins_idx},
                binary_relation_exprt{prev, ID_lt, length}},
              code_frontend_assignt{
                index_exprt{data, ii}, index_exprt{data, prev}}});
          }
          block.add(code_frontend_assignt{index_exprt{data, ins_idx}, val});
          block.add(code_frontend_assignt{
            length, plus_exprt{length, from_integer(1, python_int_type())}});
          return std::move(block);
        }
      }
    }
  }

  exprt expr = convert_expression(value);

  if(expr.is_nil())
    return code_skipt{};

  code_expressiont code_expr{expr};
  code_expr.add_source_location() = get_location(stmt);
  return std::move(code_expr);
}
