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
    // PLR: scan the lambda body for binary operations that
    // operate on float constants. If 'a + 1.1' or similar
    // appears in the body, the parameter type is widened to
    // double. This matches Python's dynamic typing where the
    // arithmetic forces the operand to be promoted to float.
    std::function<bool(const jsont &)> body_uses_float =
      [&](const jsont &n) -> bool
    {
      if(!n.is_object())
      {
        if(n.is_array())
        {
          for(const auto &e : as_array(n))
            if(body_uses_float(e))
              return true;
        }
        return false;
      }
      if(is_node_type(n, "Constant"))
      {
        const jsont &cv = json_member(n, "value");
        if(cv.is_number())
        {
          std::string vs = cv.value;
          if(
            vs.find('.') != std::string::npos ||
            vs.find('e') != std::string::npos)
            return true;
        }
      }
      // Recurse into common AST sub-fields. Covers BinOp,
      // BoolOp, Compare, UnaryOp, IfExp, Call, Tuple, List,
      // Dict, Subscript, Attribute. Any field we don't list
      // simply isn't traversed; that's a precision loss but
      // not unsoundness for this widening heuristic.
      static const std::vector<std::string> sub_fields{
        "left",
        "right",
        "operand",
        "value",
        "values",
        "elts",
        "args",
        "body",
        "test",
        "comparators",
        "func",
        "slice"};
      for(const auto &f : sub_fields)
      {
        const jsont &child = json_member(n, f);
        if(!child.is_null() && body_uses_float(child))
          return true;
      }
      return false;
    };
    bool has_float = body_uses_float(body_expr);
    // PLR §3.2: lambdas like `lambda s: s + " world"` have an
    // unannotated string parameter. Detect a string Constant
    // in the body the same way we detect floats; widen the
    // default parameter type to python_string when found.
    std::function<bool(const jsont &)> body_uses_string =
      [&](const jsont &n) -> bool
    {
      if(!n.is_object())
      {
        if(n.is_array())
        {
          for(const auto &e : as_array(n))
            if(body_uses_string(e))
              return true;
        }
        return false;
      }
      if(is_node_type(n, "Constant"))
      {
        const jsont &cv = json_member(n, "value");
        if(cv.is_string())
          return true;
      }
      static const std::vector<std::string> sub_fields{
        "left",
        "right",
        "operand",
        "value",
        "values",
        "elts",
        "args",
        "body",
        "test",
        "comparators",
        "func",
        "slice"};
      for(const auto &f : sub_fields)
      {
        const jsont &child = json_member(n, f);
        if(!child.is_null() && body_uses_string(child))
          return true;
      }
      return false;
    };
    bool has_string = body_uses_string(body_expr);
    // Float beats string when both are present (prefer numeric
    // semantics; rare in practice).
    typet default_param_type =
      has_float ? double_type()
                : (has_string ? python_string_type() : python_int_type());
    // PLR: per-parameter type inference. The body-wide
    // body_uses_float / body_uses_string heuristic gives a
    // too-coarse default — e.g. `lambda x: "pos" if x > 0 else
    // "neg"` has a string body but an int parameter `x`.
    //
    // Walk the body, looking for syntactic clues that pin a
    // specific parameter's type:
    //   - param compared with an int Constant ("x > 0")
    //     → int
    //   - param compared with a float Constant ("x > 0.5")
    //     → float
    //   - param + / += a string Constant ("s + ' world'")
    //     → string
    //   - param subscripted ("x[0]")            → string
    //   - param in BinOp with float Constant     → float
    auto infer_param_type =
      [&](const std::string &pname) -> std::optional<typet>
    {
      std::optional<typet> chosen;
      bool used_as_object = false;
      auto promote = [&](const typet &t)
      {
        if(!chosen.has_value())
          chosen = t;
        else if(t == double_type())
          chosen = t; // float wins over int / string
        else if(*chosen != double_type() && t == python_string_type())
          chosen = t;
      };
      auto is_param_name = [&](const jsont &n) -> bool
      {
        return is_node_type(n, "Name") &&
               json_string(json_member(n, "id")) == pname;
      };
      auto is_int_const = [&](const jsont &n) -> bool
      {
        if(!is_node_type(n, "Constant"))
          return false;
        const jsont &v = json_member(n, "value");
        if(!v.is_number())
          return false;
        std::string vs = v.value;
        return vs.find('.') == std::string::npos &&
               vs.find('e') == std::string::npos;
      };
      auto is_float_const = [&](const jsont &n) -> bool
      {
        if(!is_node_type(n, "Constant"))
          return false;
        const jsont &v = json_member(n, "value");
        if(!v.is_number())
          return false;
        std::string vs = v.value;
        return vs.find('.') != std::string::npos ||
               vs.find('e') != std::string::npos;
      };
      auto is_str_const = [&](const jsont &n) -> bool
      {
        if(!is_node_type(n, "Constant"))
          return false;
        return json_member(n, "value").is_string();
      };
      std::function<void(const jsont &)> walk = [&](const jsont &n) -> void
      {
        if(!n.is_object())
        {
          if(n.is_array())
            for(const auto &e : as_array(n))
              walk(e);
          return;
        }
        // Attribute access on the param (`a.attr`, or a method
        // call `a.method()` which is Call(func=Attribute(value=a)))
        // means the param is an OBJECT. Type it as the universal
        // tagged union python_value, exactly as an unannotated
        // regular-function parameter is typed, so attribute reads
        // and virtual method dispatch resolve on the argument's
        // runtime class tag instead of collapsing to a nondet int.
        // Without this, `lambda a: a.f()` typed `a` as int and every
        // member access on it was nondet (jpl / sorted-key idioms).
        if(is_node_type(n, "Attribute"))
        {
          if(is_param_name(json_member(n, "value")))
            used_as_object = true;
        }
        // Compare: param < / > / == Constant
        if(is_node_type(n, "Compare"))
        {
          const jsont &left = json_member(n, "left");
          const jsont &comps = json_member(n, "comparators");
          if(comps.is_array())
          {
            for(const auto &c : as_array(comps))
            {
              if(is_param_name(left) || is_param_name(c))
              {
                const jsont &other = is_param_name(left) ? c : left;
                if(is_int_const(other))
                  promote(python_int_type());
                else if(is_float_const(other))
                  promote(double_type());
                else if(is_str_const(other))
                  promote(python_string_type());
              }
            }
          }
        }
        // BinOp: param + / * / etc Constant
        if(is_node_type(n, "BinOp"))
        {
          const jsont &left = json_member(n, "left");
          const jsont &right = json_member(n, "right");
          if(is_param_name(left) || is_param_name(right))
          {
            const jsont &other = is_param_name(left) ? right : left;
            if(is_str_const(other))
              promote(python_string_type());
            else if(is_float_const(other))
              promote(double_type());
            else if(is_int_const(other))
              promote(python_int_type());
          }
        }
        // Subscript on param: x[i] → param is indexable.
        // Don't pin a type from this alone.
        // Recurse into common AST sub-fields.
        static const std::vector<std::string> sub_fields{
          "left",
          "right",
          "operand",
          "value",
          "values",
          "elts",
          "args",
          "body",
          "test",
          "comparators",
          "func",
          "slice",
          "orelse",
          "keywords",
          "ops"};
        for(const auto &f : sub_fields)
        {
          const jsont &child = json_member(n, f);
          if(!child.is_null())
            walk(child);
        }
      };
      walk(body_expr);
      if(used_as_object)
        return python_value_type();
      return chosen;
    };
    for(const auto &param : as_array(params))
    {
      std::string param_name = json_string(json_member(param, "arg"));
      // Use the annotation if provided; otherwise per-param
      // inference, falling back to the body-wide default.
      const jsont &annotation = json_member(param, "annotation");
      typet ptype;
      if(!annotation.is_null())
        ptype = convert_type_annotation(annotation);
      else
      {
        auto inferred = infer_param_type(param_name);
        ptype = inferred.value_or(default_param_type);
      }
      code_typet::parametert p{ptype};
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
    code_typet::parametert p{closure_capture_slot_type(outer_sym->type)};
    std::string cap_id = "python::" + lambda_name + "::" + ref;
    p.set_identifier(cap_id);
    p.set_base_name(ref);
    parameters.push_back(p);
    // Record capture for call-site argument passing
    closure_captures["python::" + lambda_name].push_back(
      {outer_id, ref, closure_capture_slot_type(outer_sym->type)});
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

  // Save/restore pending_checks across body conversion. Pending
  // checks emitted while converting the lambda body (e.g. division
  // by zero, list index out of range) belong inside the lambda's
  // function body, not at the lambda's definition site. Without
  // this, `div = lambda x, y: x / y` makes the outer scope see the
  // inner ZeroDivisionError check on every reference of `div` —
  // even when never called.
  std::vector<codet> saved_pending_checks;
  saved_pending_checks.swap(pending_checks);
  exprt body_val = convert_expression(body_expr);
  std::vector<codet> body_checks;
  body_checks.swap(pending_checks);
  saved_pending_checks.swap(pending_checks);
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
  for(auto &c : body_checks)
    body.add(std::move(c));
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
  // Fat-closure: eagerly register a capturing lambda so that any
  // higher-order callee converted later (e.g. `def apply(f): return
  // f()`) emits a runtime dispatch branch for it. Registration is by
  // function id (deduped); boxing reuses the same index.
  if(closure_captures.count("python::" + lambda_name))
    register_closure(func_id);
  return func_sym.symbol_expr();
}
