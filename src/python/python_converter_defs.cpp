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

  std::string func_name = json_string(json_member(stmt, "name"));
  source_locationt loc = get_location(stmt);

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

    // PLR §4.2.1: Class instances are passed by reference.
    if(
      param_type.id() == ID_struct &&
      id2string(to_struct_type(param_type).get_tag()).find("python_class_") !=
        std::string::npos &&
      param_name != "self")
    {
      param_type = pointer_type(param_type);
    }

    code_typet::parametert p{param_type};
    p.set_identifier("python::" + func_name + "::" + param_name);
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

  // PLR §8.7: keyword-only arguments (after * in parameter list)
  const jsont &kwonlyargs = json_member(args_node, "kwonlyargs");
  if(kwonlyargs.is_array())
  {
    for(const auto &param : as_array(kwonlyargs))
    {
      std::string param_name = json_string(json_member(param, "arg"));
      const jsont &annotation = json_member(param, "annotation");
      typet param_type = annotation.is_null()
                           ? python_int_type()
                           : convert_type_annotation(annotation);
      code_typet::parametert p{param_type};
      p.set_identifier("python::" + func_name + "::" + param_name);
      p.set_base_name(param_name);
      parameters.push_back(p);
    }
  }

  // PLR §8.7: *args — catch-all positional argument tuple.
  // We model it as a list (our tuple model is effectively a list here).
  const jsont &vararg = json_member(args_node, "vararg");
  std::string varargs_name;
  if(!vararg.is_null())
  {
    varargs_name = json_string(json_member(vararg, "arg"));
    typet va_type = python_list_type(python_value_type());
    code_typet::parametert p{va_type};
    p.set_identifier("python::" + func_name + "::" + varargs_name);
    p.set_base_name(varargs_name);
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
        p.set_identifier("python::" + func_name + "::" + param_name);
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
    p.set_identifier("python::" + func_name + "::" + kwargs_name);
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

  // Return type
  bool has_yield = false;
  const jsont &returns = json_member(stmt, "returns");
  typet return_type = empty_typet{};
  if(!returns.is_null())
  {
    return_type = convert_type_annotation(returns);
    annotated_return_functions.insert(func_name);
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
      return_type = empty_typet{};
  }

  code_typet func_type{parameters, return_type};

  // Add closure captures as extra parameters
  irep_idt func_qid{"python::" + func_name};
  auto cap_it = closure_captures.find(id2string(func_qid));
  if(cap_it != closure_captures.end())
  {
    for(const auto &[outer_id, name, type] : cap_it->second)
    {
      code_typet::parametert p{type};
      std::string cap_param_id = "python::" + func_name + "::" + name;
      p.set_identifier(cap_param_id);
      p.set_base_name(name);
      parameters.push_back(p);
    }
    func_type = code_typet{parameters, return_type};
  }

  if(has_yield)
    generator_functions.insert(func_name);

  // Create function symbol BEFORE converting the body
  // (so recursive calls can find it)
  irep_idt symbol_id{"python::" + func_name};

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
  }

  // Convert function body
  std::string saved_function = current_function;
  auto saved_globals = global_names;
  if(!current_function.empty())
    enclosing_functions.push_back(current_function);
  current_function = func_name;
  global_names.clear();
  nonlocal_names.clear();

  // For generator functions, create __gen_result list
  bool is_generator = generator_functions.count(func_name) > 0;
  irep_idt gen_result_id;
  if(is_generator)
  {
    std::string grn = "__gen_result_" + func_name;
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

  // For generators, initialize __gen_result.length = 0
  if(is_generator)
  {
    body_block.add(code_frontend_assignt{
      member_exprt{
        symbol_table.lookup_ref(gen_result_id).symbol_expr(),
        "length",
        signedbv_typet{64}},
      from_integer(0, signedbv_typet{64})});
  }

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
        std::string nested_name = json_string(json_member(s, "name"));
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
          std::string var_id = "python::" + func_name + "::" + ref;
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
    for(const auto &s : as_array(body))
      body_block.add(convert_statement(s));
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
    body_block.add(code_frontend_returnt{
      symbol_table.lookup_ref(gen_result_id).symbol_expr()});
  }
  else if(return_type.id() != ID_empty)
  {
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
    body_block.add(code_frontend_returnt{none_expr});
  }

  // Update the symbol with the body
  symbolt *sym_ptr = symbol_table.get_writeable(symbol_id);
  if(sym_ptr != nullptr)
    sym_ptr->value = body_block;

  // Function definitions don't produce executable code at the call site
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
          typet attr_type =
            convert_type_annotation(json_member(item, "annotation"));
          components.push_back(struct_typet::componentt{attr_name, attr_type});
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
          components.push_back(struct_typet::componentt{attr_name, attr_type});
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
          }
        }

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
        }

        // Convert method body
        {
          std::string saved_func = current_function;
          if(!current_function.empty())
            enclosing_functions.push_back(current_function);
          current_function = class_name + "::" + method_name;

          code_blockt method_body;
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
                      unpack_td_name = json_string(json_member(slc, "id"));
                  }
                }
              }
            }
          }
          const jsont &method_body_json = json_member(item, "body");
          if(!skip_body && method_body_json.is_array())
          {
            for(const auto &s : as_array(method_body_json))
              method_body.add(convert_statement(s));
          }

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
          if(val.type() != data_type.element_type())
            val = safe_typecast(val, data_type.element_type());
          code_blockt block;
          for(int i = PYTHON_MAX_LIST_LENGTH - 1; i >= 1; i--)
          {
            exprt ii = from_integer(i, python_int_type());
            exprt prev = from_integer(i - 1, python_int_type());
            block.add(code_ifthenelset{
              and_exprt{
                binary_relation_exprt{prev, ID_ge, idx_expr},
                binary_relation_exprt{prev, ID_lt, length}},
              code_frontend_assignt{
                index_exprt{data, ii}, index_exprt{data, prev}}});
          }
          block.add(code_frontend_assignt{index_exprt{data, idx_expr}, val});
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
