/// \\file
/// TypeScript to GOTO converter — split implementation

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/floatbv_expr.h>
#include <util/ieee_float.h>
#include <util/irep.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/symbol.h>

#include <goto-programs/goto_functions.h>

#include "typescript_converter.h"
#include "typescript_types.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>

void typescript_convertert::convert_function_declaration(const jsont &node)
{
  std::string func_name =
    json_string(json_member(json_member(node, "name"), "text"));
  if(func_name.empty())
    return;
  convert_function_declaration_with_name(node, func_name);
}

// ES2024 sec-function-definitions
// TSH: Functions > Function Types
void typescript_convertert::convert_function_declaration_with_name(
  const jsont &node,
  const std::string &func_name)
{
  // Detect generic functions (type parameters present)
  // Skip if we're currently instantiating (current_generic_concrete is set)
  const jsont &type_params = json_member(node, "typeParameters");
  if(
    type_params.is_array() && !to_json_array(type_params).empty() &&
    current_generic_concrete.empty())
  {
    // Store AST for monomorphization at call sites
    generic_functions[func_name] = node;
    return;
  }

  // Get return type
  std::string ret_type_str = json_string(json_member(node, "_returnType"));

  // ES2024 §27.5: Generator functions. When isGenerator is true,
  // scan the body for YieldExpression nodes, collect their values,
  // and model the function as returning a generator struct:
  //   { __state: signedbv[32], __count: signedbv[32], __values: T[N] }
  // The caller's gen.next() reads __values[__state++].
  const jsont &is_gen = json_member(node, "isGenerator");
  if(is_gen.is_true())
  {
    // Collect yield values from the body.
    std::vector<exprt> yield_values;
    std::function<void(const jsont &)> scan_yields = [&](const jsont &n)
    {
      if(!n.is_object())
        return;
      std::string nk = json_string(json_member(n, "_kind"));
      if(nk == "YieldExpression")
      {
        const jsont &expr = json_member(n, "expression");
        if(expr.is_object())
          yield_values.push_back(convert_expression(expr));
        else
          yield_values.push_back(ts_nan_with_payload(TS_NAN_PAYLOAD_UNDEFINED));
        return; // don't recurse into yield's own expression
      }
      for(const auto &kv : to_json_object(n))
      {
        if(kv.second.is_object())
          scan_yields(kv.second);
        else if(kv.second.is_array())
          for(const auto &elem : to_json_array(kv.second))
            scan_yields(elem);
      }
    };
    const jsont &body = json_member(node, "body");
    if(body.is_object())
      scan_yields(body);

    std::size_t count = yield_values.size();
    // Build the generator struct type.
    typet elem_type = count > 0 ? yield_values[0].type() : double_type();
    std::size_t max_yields = std::max<std::size_t>(count, 1);
    array_typet vals_type{
      elem_type, from_integer(max_yields, signedbv_typet{64})};
    struct_typet gen_type;
    gen_type.components().push_back(
      struct_typet::componentt{"__state", signedbv_typet{32}});
    gen_type.components().push_back(
      struct_typet::componentt{"__count", signedbv_typet{32}});
    gen_type.components().push_back(
      struct_typet::componentt{"__values", vals_type});
    gen_type.set_tag("typescript_generator");

    // Build the function body: return the generator struct with
    // state=0, count=N, values=[v0, v1, ...].
    exprt::operandst val_elts;
    for(auto &v : yield_values)
    {
      if(v.type() != elem_type)
        v = typecast_exprt{v, elem_type};
      val_elts.push_back(v);
    }
    while(val_elts.size() < max_yields)
      val_elts.push_back(from_integer(0, elem_type));
    exprt gen_val = struct_exprt{
      {from_integer(0, signedbv_typet{32}),
       from_integer(count, signedbv_typet{32}),
       array_exprt{std::move(val_elts), vals_type}},
      gen_type};

    // Register the function symbol.
    irep_idt func_id{"typescript::" + func_name};
    code_typet func_type{code_typet::parameterst{}, gen_type};
    if(symbol_table.lookup(func_id) == nullptr)
    {
      symbolt fs{func_id, func_type, "typescript"};
      fs.base_name = func_name;
      fs.value = code_frontend_returnt{gen_val};
      symbol_table.add(fs);
    }
    // Store the generator type for next() dispatch.
    class_types["__gen_" + func_name] = gen_type;
    return;
  }

  typet ret_type =
    ret_type_str.empty() ? empty_typet{} : convert_type(ret_type_str);
  // If all parameters are integer, return type likely is too
  if(ret_type_str == "number" && integer_inference)
  {
    // Check if function name is in integer_vars context
    std::string scope_key = func_name + "::__return";
    auto it = inferred_num_kind.find(scope_key);
    if(it != inferred_num_kind.end())
      ret_type = number_type_for(scope_key);
  }

  // Get parameters
  code_typet::parameterst params;
  const jsont &param_nodes = json_member(node, "parameters");
  if(param_nodes.is_array())
  {
    for(const auto &p : to_json_array(param_nodes))
    {
      std::string pname =
        json_string(json_member(json_member(p, "name"), "text"));
      std::string ptype_str = json_string(json_member(p, "_type"));
      typet ptype = convert_type(ptype_str);

      // Integer inference for parameters
      if(ptype_str == "number" && integer_inference)
      {
        std::string scope_key = func_name + "::" + pname;
        typet inferred = number_type_for(scope_key);
        if(inferred.id() != ID_floatbv)
          ptype = inferred;
      }

      // Track rest parameters
      if(json_member(p, "isRest").is_true())
        rest_param_functions.insert(irep_idt{"typescript::" + func_name});

      code_typet::parametert param{ptype};
      param.set_identifier("typescript::" + func_name + "::" + pname);
      param.set_base_name(pname);
      params.push_back(param);
    }
  }

  code_typet func_type{params, ret_type};
  irep_idt func_id{"typescript::" + func_name};

  // Create function symbol
  symbolt func_sym{func_id, func_type, "typescript"};
  func_sym.base_name = func_name;
  func_sym.location = get_location(node);
  func_sym.is_lvalue = true;

  // Create parameter symbols
  for(const auto &param : params)
  {
    irep_idt pid = param.get_identifier();
    {
      symbolt psym{pid, param.type(), "typescript"};
      psym.base_name = id2string(param.get_base_name());
      psym.is_parameter = true;
      psym.is_lvalue = true;
      psym.is_state_var = true;
      symbol_table.add(psym);
    }
  }

  // Convert function body
  // Detect captured variables from enclosing scope
  // If this is a nested function (current_function is set), scan body for
  // identifiers that match enclosing function's parameters/locals
  std::vector<std::pair<std::string, typet>> captured_vars;
  if(!current_function.empty())
  {
    // Collect identifiers used in the body
    std::function<void(const jsont &)> scan = [&](const jsont &n)
    {
      if(!n.is_object())
        return;
      std::string k = json_string(json_member(n, "_kind"));
      if(k == "Identifier")
      {
        std::string text = json_string(json_member(n, "text"));
        if(text.empty())
          return;
        // Check if it's a parameter of THIS function
        bool is_local = false;
        for(const auto &p : params)
          if(id2string(p.get_base_name()) == text)
            is_local = true;
        if(is_local)
          return;
        // Check if it's a variable in the enclosing scope
        std::string outer_id = "typescript::" + current_function + "::" + text;
        const symbolt *outer_sym = symbol_table.lookup(irep_idt{outer_id});
        if(outer_sym != nullptr)
        {
          // Check not already captured
          bool already = false;
          for(const auto &cv : captured_vars)
            if(cv.first == text)
              already = true;
          if(!already)
            captured_vars.emplace_back(text, outer_sym->type);
        }
      }
      // Recurse into json children
      auto recurse_json = [&](const jsont &child)
      {
        if(child.is_object())
          scan(child);
        else if(child.is_array())
          for(const auto &c : to_json_array(child))
            scan(c);
      };
      // Check known child fields
      static const char *fields[] = {
        "expression",      "left",          "right",         "body",
        "statements",      "thenStatement", "elseStatement", "statement",
        "arguments",       "elements",      "properties",    "declarations",
        "declarationList", "initializer",   "condition",     "incrementor",
        "operand",         "head",          "templateSpans", "name",
        "members",         "parameters",    "_children",     nullptr};
      for(const char **f = fields; *f; ++f)
      {
        const jsont &child = json_member(n, *f);
        recurse_json(child);
      }
    };
    const jsont &body_node = json_member(node, "body");
    if(body_node.is_object())
      scan(body_node);

    // Add captured vars as extra parameters
    // Only add as parameters if the variable is a PARAMETER of the outer
    // function (immutable capture). For local variables (let/const in outer
    // scope), access them directly via their symbol (mutable sharing).
    for(const auto &[cv_name, cv_type] : captured_vars)
    {
      std::string outer_id = "typescript::" + current_function + "::" + cv_name;
      const symbolt *outer_sym = symbol_table.lookup(irep_idt{outer_id});
      bool is_outer_param = outer_sym && outer_sym->is_parameter;
      if(is_outer_param)
      {
        // Immutable capture: pass as extra parameter
        code_typet::parametert cp{cv_type};
        cp.set_identifier("typescript::" + func_name + "::" + cv_name);
        cp.set_base_name(cv_name);
        params.push_back(cp);
        irep_idt cpid{"typescript::" + func_name + "::" + cv_name};
        if(symbol_table.lookup(cpid) == nullptr)
        {
          symbolt cps{cpid, cv_type, "typescript"};
          cps.base_name = cv_name;
          cps.is_parameter = true;
          cps.is_lvalue = true;
          cps.is_state_var = true;
          symbol_table.add(cps);
        }
      }
      else
      {
        // Mutable sharing: do NOT create a local symbol.
        // The nested function will access the outer variable directly
        // via convert_identifier's scope fallback mechanism.
      }
    }
    // Update function type with new params
    func_type = code_typet{params, ret_type};
    func_sym.type = func_type;
    // Store capture info for call sites
    if(!captured_vars.empty())
    {
      auto &cv = captured_var_map[func_id];
      for(const auto &[cv_name, cv_type] : captured_vars)
      {
        std::string outer_id =
          "typescript::" + current_function + "::" + cv_name;
        cv.emplace_back(cv_name, irep_idt{outer_id});
      }
    }
  }

  // Add function symbol to table BEFORE body conversion
  // (so return type can be looked up during body conversion)
  if(symbol_table.lookup(func_id) == nullptr)
    symbol_table.add(func_sym);
  else
  {
    // Update type if we added captured params
    symbol_table.get_writeable_ref(func_id).type = func_type;
  }

  // Store default parameter values
  {
    const jsont &fn_params = json_member(node, "parameters");
    if(fn_params.is_array())
    {
      std::size_t pi = 0;
      for(const auto &p : to_json_array(fn_params))
      {
        const jsont &def_init = json_member(p, "initializer");
        if(def_init.is_object())
        {
          exprt def_val = convert_expression(def_init);
          if(!def_val.is_nil())
            default_values[func_id][pi] = def_val;
        }
        pi++;
      }
    }
  }
  const jsont &body = json_member(node, "body");
  if(body.is_object())
  {
    std::string saved_function = current_function;
    current_function = func_name;
    std::string body_kind = json_string(json_member(body, "_kind"));
    codet body_code = code_skipt{};
    if(body_kind == "Block")
    {
      body_code = convert_block(body);
    }
    else
    {
      // Concise arrow function: body is an expression → wrap in return
      exprt expr = convert_expression(body);
      if(!expr.is_nil())
        body_code = code_frontend_returnt{expr};
    }
    current_function = saved_function;
    func_sym.value = body_code;
  }

  if(symbol_table.lookup(func_id) == nullptr)
    symbol_table.add(func_sym);
  else if(!func_sym.value.is_nil())
  {
    // Update existing symbol with body (e.g., declare → definition)
    symbol_table.get_writeable_ref(func_id).value = func_sym.value;
    symbol_table.get_writeable_ref(func_id).type = func_sym.type;
  }
}

// --- Module body ---

// ES2024 sec-module-semantics-runtime-semantics-evaluation
// TSH: Modules > Export/Import
void typescript_convertert::convert_module_body(const jsont &statements)
{
  if(!statements.is_array())
    return;

  // Integer type inference: determine which variables can use integer types
  infer_integer_types(statements);

  // Pre-scan: collect all function names that are called or
  // otherwise referenced anywhere in the module body (to skip
  // converting unused functions). A function may be referenced by
  // name without being directly called — e.g. `const g = f; g(x)`
  // or `arr.forEach(f)` — so we cannot limit the scan to
  // CallExpression callees. Any Identifier whose text matches a
  // top-level function name is treated as a reference.
  std::set<std::string> called_names;
  // First, enumerate top-level function-declaration names. We use
  // this as a filter: only identifiers matching one of these count
  // as function references (so we don't falsely trigger on every
  // variable name).
  std::set<std::string> top_level_fn_names;
  for(const auto &s : to_json_array(statements))
  {
    if(json_string(json_member(s, "_kind")) == "FunctionDeclaration")
    {
      std::string n = json_string(json_member(json_member(s, "name"), "text"));
      if(!n.empty())
        top_level_fn_names.insert(n);
    }
  }
  std::function<void(const jsont &)> scan_calls = [&](const jsont &node)
  {
    if(!node.is_object())
      return;
    std::string nk = json_string(json_member(node, "_kind"));
    if(nk == "CallExpression")
    {
      const jsont &ce = json_member(node, "expression");
      std::string ck = json_string(json_member(ce, "_kind"));
      if(ck == "Identifier")
        called_names.insert(json_string(json_member(ce, "text")));
    }
    // Also: bare Identifier references to top-level function names.
    // These cover `const g = f`, `cb(f)`, `return f`, etc. We only
    // count identifiers that match a known top-level function so
    // that we don't over-expand the conversion set.
    if(nk == "Identifier")
    {
      std::string text = json_string(json_member(node, "text"));
      if(top_level_fn_names.count(text))
        called_names.insert(text);
    }
    // Recurse into all object members
    if(node.is_object())
    {
      for(const auto &kv : to_json_object(node))
      {
        if(kv.second.is_object())
          scan_calls(kv.second);
        else if(kv.second.is_array())
          for(const auto &elem : to_json_array(kv.second))
            scan_calls(elem);
      }
    }
  };
  for(const auto &stmt : to_json_array(statements))
    scan_calls(stmt);

  // Process all statements in order
  // Function declarations are converted only if called.
  code_blockt start_body;
  for(const auto &stmt : to_json_array(statements))
  {
    std::string kind = json_string(json_member(stmt, "_kind"));
    if(kind == "FunctionDeclaration")
    {
      std::string fname =
        json_string(json_member(json_member(stmt, "name"), "text"));
      if(called_names.count(fname) || fname.empty())
        convert_function_declaration(stmt);
      continue;
    }
    // Namespace: ModuleDeclaration with an inner ModuleBlock
    if(kind == "ModuleDeclaration")
    {
      // Extract inner statements from _children[1] (ModuleBlock)
      const jsont &children = json_member(stmt, "_children");
      if(children.is_array() && to_json_array(children).size() >= 2)
      {
        auto it = to_json_array(children).begin();
        std::string ns_name = json_string(json_member(*it, "text"));
        ++it;
        const jsont &block = *it;
        const jsont &inner = json_member(block, "_children");
        if(inner.is_array())
        {
          // Recursively process namespace contents with qualified names
          std::string saved = current_function;
          current_function = ns_name;
          for(const auto &inner_stmt : to_json_array(inner))
          {
            std::string ik = json_string(json_member(inner_stmt, "_kind"));
            if(ik == "FunctionDeclaration")
            {
              std::string fname = json_string(
                json_member(json_member(inner_stmt, "name"), "text"));
              // Register as ns_name::fname
              convert_function_declaration_with_name(
                inner_stmt, ns_name + "::" + fname);
              // Also register a bare name alias for lookup
              irep_idt qid{"typescript::" + ns_name + "::" + fname};
              irep_idt aid{"typescript::" + fname + "__ns_" + ns_name};
              // Actually simpler: just register the fname too
            }
            else
            {
              codet code = convert_statement(inner_stmt);
              start_body.add(std::move(code));
            }
          }
          current_function = saved;
        }
      }
      continue;
    }
    codet code = convert_statement(stmt);
    start_body.add(std::move(code));
  }

  // Create __CPROVER__start function
  // Initialize __CPROVER_rounding_mode at the start
  code_blockt full_body;
  {
    irep_idt rm_id{"__CPROVER_rounding_mode"};
    {
      symbolt rm_sym{rm_id, signedbv_typet{32}, "typescript"};
      rm_sym.base_name = "__CPROVER_rounding_mode";
      rm_sym.is_lvalue = true;
      rm_sym.is_state_var = true;
      rm_sym.is_static_lifetime = true;
      symbol_table.add(rm_sym);
    }
    const symbolt &rm = symbol_table.lookup_ref(rm_id);
    full_body.add(code_frontend_assignt{
      rm.symbol_expr(), from_integer(0, signedbv_typet{32})});
  }
  for(auto &stmt : start_body.statements())
    full_body.add(std::move(stmt));

  std::string start_name = "__CPROVER__start";
  irep_idt start_id{start_name};

  {
    code_typet start_type{{}, empty_typet{}};
    symbolt start_sym{start_id, start_type, "typescript"};
    start_sym.base_name = start_name;
    start_sym.is_lvalue = true;
    start_sym.value = full_body;
    symbol_table.add(start_sym);
  }
}

// --- Main entry point ---
