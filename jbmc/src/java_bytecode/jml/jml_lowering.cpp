/*******************************************************************\

Module: JML-to-GOTO Lowering

Author: Kiro (AI agent)

\*******************************************************************/

#include "jml_lowering.h"

#include <util/arith_tools.h>
#include <util/c_types.h>
#include <util/cprover_prefix.h>
#include <util/fresh_symbol.h>
#include <util/mathematical_expr.h>
#include <util/namespace.h>
#include <util/std_code.h>
#include <util/std_expr.h>

#include <ansi-c/c_expr.h>

#include <goto-programs/goto_model.h>

#include <langapi/language_util.h>

namespace
{

/// Resolve unresolved JML expressions (jml_field_access,
/// jml_method_call, untyped symbols) against the symbol table.
/// This is the "Phase 3 resolution" that the parser deferred.
exprt resolve_jml_expr(
  const exprt &e,
  const irep_idt &method_id,
  const irep_idt &class_id,
  const namespacet &ns)
{
  // Resolve symbol_exprt with empty type: look up as parameter
  // or field of the enclosing class.
  if(e.id() == ID_symbol && e.type().id() == ID_empty)
  {
    const irep_idt &name = to_symbol_expr(e).get_identifier();
    const std::string name_str = id2string(name);

    // Try as a parameter: method_id::name
    const irep_idt param_id =
      id2string(method_id) + "::" + name_str;
    if(const auto *sym = ns.get_symbol_table().lookup(param_id))
      return sym->symbol_expr();

    // Try as a static field: class_id.name
    const irep_idt field_id =
      id2string(class_id) + "." + name_str;
    if(const auto *sym = ns.get_symbol_table().lookup(field_id))
      return sym->symbol_expr();

    // Try as a parameter with arg prefix: method_id::arg0<name>
    // JBMC names parameters as arg0a, arg1i, etc. — try prefix match.
    const symbolt *func_sym = ns.get_symbol_table().lookup(method_id);
    if(func_sym != nullptr && func_sym->type.id() == ID_code)
    {
      const auto &params = to_code_type(func_sym->type).parameters();
      for(const auto &p : params)
      {
        const std::string base = id2string(p.get_base_name());
        if(base == name_str || base.find(name_str) != std::string::npos)
        {
          if(!p.get_identifier().empty())
          {
            if(const auto *psym =
                 ns.get_symbol_table().lookup(p.get_identifier()))
              return psym->symbol_expr();
          }
        }
      }
    }

    // Leave unresolved — will produce a verification failure with
    // a clear "unknown symbol" message.
    return e;
  }

  // Resolve jml_field_access: obj.field → member_exprt or
  // static field symbol
  if(e.id() == ID_member && e.type().id() == ID_empty)
  {
    // Defer to Phase 3 full resolution (needs type info from obj)
    return e;
  }

  // Recurse into operands
  exprt result = e;
  for(auto &op : result.operands())
    op = resolve_jml_expr(op, method_id, class_id, ns);
  return result;
}

} // namespace

std::set<irep_idt> lower_jml_contracts(
  goto_modelt &goto_model,
  const jml_contract_mapt &contracts)
{
  std::set<irep_idt> annotated_functions;
  const namespacet ns{goto_model.symbol_table};

  for(const auto &[method_id, spec] : contracts)
  {
    auto fn_it = goto_model.goto_functions.function_map.find(method_id);
    if(fn_it == goto_model.goto_functions.function_map.end())
      continue;

    auto &body = fn_it->second.body;
    if(body.instructions.empty())
      continue;

    const symbolt &func_sym = ns.lookup(method_id);
    const code_typet &func_type = to_code_type(func_sym.type);
    const irep_idt class_id = func_type.get(ID_C_class);

    // Collect clauses by kind
    exprt::operandst requires_exprs;
    exprt::operandst ensures_exprs;
    exprt::operandst assigns_exprs;
    exprt::operandst invariant_exprs;
    exprt::operandst decreases_exprs;

    for(const auto &clause : spec.clauses)
    {
      if(clause.expr.is_nil())
        continue;

      exprt resolved =
        resolve_jml_expr(clause.expr, method_id, class_id, ns);

      switch(clause.kind)
      {
      case jml_clauset::kindt::REQUIRES:
        requires_exprs.push_back(resolved);
        break;
      case jml_clauset::kindt::ENSURES:
        ensures_exprs.push_back(resolved);
        break;
      case jml_clauset::kindt::ASSIGNABLE:
        assigns_exprs.push_back(resolved);
        break;
      case jml_clauset::kindt::INVARIANT:
        invariant_exprs.push_back(resolved);
        break;
      case jml_clauset::kindt::DECREASES:
        decreases_exprs.push_back(resolved);
        break;
      default:
        break;
      }
    }

    if(requires_exprs.empty() && ensures_exprs.empty())
      continue;

    // Emit body-level ASSUME for requires at function entry
    auto first_it = body.instructions.begin();
    for(const auto &req : requires_exprs)
    {
      source_locationt loc = first_it->source_location();
      loc.set_comment("JML requires");
      loc.set_property_class("precondition");
      body.insert_before(
        first_it, goto_programt::make_assumption(req, loc));
    }

    // Emit body-level ASSERT for ensures at return sites
    // Find all SET_RETURN_VALUE or END_FUNCTION instructions
    for(auto it = body.instructions.begin();
        it != body.instructions.end();
        ++it)
    {
      if(it->type() == END_FUNCTION)
      {
        for(const auto &ens : ensures_exprs)
        {
          source_locationt loc = it->source_location();
          loc.set_comment("JML ensures");
          loc.set_property_class("postcondition");
          body.insert_before(
            it, goto_programt::make_assertion(ens, loc));
        }
      }
    }

    // Emit loop invariants at loop heads (back-edge targets).
    // A loop head is any instruction that is the target of a
    // backward GOTO (i.e., a GOTO whose target has a lower
    // location number than the GOTO itself).
    if(!invariant_exprs.empty())
    {
      // Find loop heads
      std::vector<goto_programt::targett> loop_heads;
      for(auto it = body.instructions.begin();
          it != body.instructions.end();
          ++it)
      {
        if(it->is_goto())
        {
          for(const auto &target : it->targets)
          {
            if(target->location_number <= it->location_number)
              loop_heads.push_back(target);
          }
        }
      }

      // Insert invariant assertions at each loop head
      for(auto head : loop_heads)
      {
        for(const auto &inv : invariant_exprs)
        {
          source_locationt loc = head->source_location();
          loc.set_comment("JML loop invariant");
          loc.set_property_class("loop-invariant");
          body.insert_before(
            head, goto_programt::make_assertion(inv, loc));
        }
      }
    }

    // Emit decreases (variant function) checks at loop heads.
    // For each loop head, assert that the variant is non-negative
    // (termination argument).
    if(!decreases_exprs.empty())
    {
      std::vector<goto_programt::targett> loop_heads;
      for(auto it = body.instructions.begin();
          it != body.instructions.end();
          ++it)
      {
        if(it->is_goto())
        {
          for(const auto &target : it->targets)
          {
            if(target->location_number <= it->location_number)
              loop_heads.push_back(target);
          }
        }
      }

      for(auto head : loop_heads)
      {
        for(const auto &dec : decreases_exprs)
        {
          source_locationt loc = head->source_location();
          loc.set_comment("JML decreases (non-negative)");
          loc.set_property_class("loop-variant");
          exprt non_neg = binary_relation_exprt(
            dec, ID_ge, from_integer(0, dec.type()));
          body.insert_before(
            head, goto_programt::make_assertion(non_neg, loc));
        }
      }
    }

    body.update();
    annotated_functions.insert(method_id);

    // Populate code_with_contract_typet on the symbol (for DFCC)
    auto sym_it = goto_model.symbol_table.get_writeable(method_id);
    if(sym_it != nullptr)
    {
      const code_typet &existing_type = to_code_type(sym_it->type);

      // Build parameter symbols for lambda wrapping
      binding_exprt::variablest parameter_syms;
      if(existing_type.return_type().id() != ID_empty)
      {
        parameter_syms.push_back(symbol_exprt(
          CPROVER_PREFIX "return_value", existing_type.return_type()));
      }
      for(const auto &p : existing_type.parameters())
      {
        if(!p.get_identifier().empty())
          parameter_syms.push_back(
            symbol_exprt(p.get_identifier(), p.type()));
      }

      auto wrap_lambda = [&](const exprt &e) -> exprt
      {
        lambda_exprt lambda(parameter_syms, e);
        lambda.add_source_location() = e.source_location();
        return std::move(lambda);
      };

      code_with_contract_typet contract_type(
        existing_type.parameters(), existing_type.return_type());
      static_cast<typet &>(contract_type)
        .set(ID_C_class, existing_type.get(ID_C_class));

      auto &c_req = contract_type.c_requires();
      auto &c_ens = contract_type.c_ensures();
      auto &c_asg = contract_type.c_assigns();

      for(const auto &e : requires_exprs)
        c_req.push_back(wrap_lambda(e));
      for(const auto &e : ensures_exprs)
        c_ens.push_back(wrap_lambda(e));
      for(const auto &e : assigns_exprs)
        c_asg.push_back(wrap_lambda(e));

      sym_it->type = contract_type;

      // Insert parallel contract:: symbol
      const irep_idt contract_id = "contract::" + id2string(method_id);
      if(!goto_model.symbol_table.has_symbol(contract_id))
      {
        symbolt contract;
        contract.name = contract_id;
        contract.base_name = sym_it->base_name;
        contract.pretty_name = sym_it->pretty_name;
        contract.is_property = false;
        contract.type = contract_type;
        contract.mode = sym_it->mode;
        contract.module = sym_it->module;
        contract.location = sym_it->location;
        goto_model.symbol_table.insert(std::move(contract));
      }
    }
  }

  return annotated_functions;
}
