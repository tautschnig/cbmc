/// Python to GOTO converter — Lambda (PLR §6.14) implementation.
///
/// Extracted from python_converter.cpp to reduce the main file size.
/// All logic and class-member state remains unchanged — this file is
/// a pure source-split.

#include <util/c_types.h>
#include <util/json.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/symbol.h>

#include "python_converter.h"
#include "python_converter_helpers.h"
#include "python_types.h"
#include "python_value_type.h"

#include <set>

// PLR §6.14: Lambdas
// "Lambda expressions are used to create anonymous functions."
exprt python_convertert::convert_lambda(const jsont &expr)
{
  std::string lambda_name = "__lambda_" + std::to_string(lambda_counter++);
  source_locationt loc = get_location(expr);

  const jsont &args_node = json_member(expr, "args");
  const jsont &params = json_member(args_node, "args");
  const jsont &body_expr = json_member(expr, "body");

  code_typet::parameterst parameters;
  if(params.is_array())
  {
    for(const auto &param : as_array(params))
    {
      std::string param_name = json_string(json_member(param, "arg"));
      code_typet::parametert p{python_int_type()};
      p.set_identifier("python::" + lambda_name + "::" + param_name);
      p.set_base_name(param_name);
      parameters.push_back(p);
    }
  }

  std::string saved_func = current_function;
  if(!current_function.empty())
    enclosing_functions.push_back(current_function);
  current_function = lambda_name;

  // Detect free variables in lambda body (closure capture)
  std::set<std::string> lambda_params;
  for(const auto &p : parameters)
    lambda_params.insert(std::string{id2string(p.get_base_name())});
  std::set<std::string> refs;
  collect_name_refs(body_expr, refs);
  for(const auto &ref : refs)
  {
    if(lambda_params.count(ref))
      continue; // local param
    // Check if it's a variable in the enclosing scope
    std::string outer_id = "python::" + saved_func + "::" + ref;
    const symbolt *outer_sym = symbol_table.lookup(irep_idt{outer_id});
    if(outer_sym == nullptr)
      continue;
    // Add as extra parameter
    code_typet::parametert p{outer_sym->type};
    std::string cap_id = "python::" + lambda_name + "::" + ref;
    p.set_identifier(cap_id);
    p.set_base_name(ref);
    parameters.push_back(p);
    // Record capture for call-site argument passing
    closure_captures["python::" + lambda_name].push_back(
      {outer_id, ref, outer_sym->type});
  }

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

  exprt body_val = convert_expression(body_expr);
  current_function = saved_func;
  if(!enclosing_functions.empty() && enclosing_functions.back() == saved_func)
    enclosing_functions.pop_back();

  if(body_val.is_nil())
    return nil_exprt{};

  typet return_type = body_val.type();
  code_typet func_type{parameters, return_type};

  irep_idt func_id{"python::" + lambda_name};
  symbolt func_sym{func_id, func_type, "python"};
  func_sym.base_name = lambda_name;
  func_sym.location = loc;
  func_sym.is_lvalue = true;
  code_blockt body;
  body.add(code_frontend_returnt{body_val});
  func_sym.value = body;

  // PLR §6.13: a lambda whose body is itself a callable expression
  // (lambda x: lambda y: ...) is the closure-creating idiom. Register
  // the outer-to-inner mapping so convert_assign's "lambda-returning
  // function" detection can rewrite `g = outer(args)` into a direct
  // closure-bound call to the inner lambda — without this CBMC's
  // symex would see `g := <code-typed return value>` and abort with
  // "assignment to 'symbol' not handled".
  if(body_val.id() == ID_symbol && body_val.type().id() == ID_code)
  {
    lambda_returning_functions[lambda_name] =
      to_symbol_expr(body_val).get_identifier();
  }

  symbol_table.add(func_sym);
  return func_sym.symbol_expr();
}
