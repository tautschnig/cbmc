/*******************************************************************\

Module: Java Bytecode Contracts Support

Author: Michael Tautschnig (via Kiro)

Date: May 2026

\*******************************************************************/

/// \file
/// Recognize and lower JVerify-style contract calls in GOTO programs.

#include "java_bytecode_contracts.h"

#include <util/prefix.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/arith_tools.h>

#include <goto-programs/goto_model.h>

static const std::string jverify_prefix =
  "java::org.strata.jverify.JVerify.";

bool is_jverify_contract_method(const irep_idt &method_id)
{
  return classify_jverify_call(method_id) !=
         jverify_contract_kindt::NOT_A_CONTRACT;
}

jverify_contract_kindt classify_jverify_call(const irep_idt &method_id)
{
  const std::string id_str = id2string(method_id);

  if(!has_prefix(id_str, jverify_prefix))
    return jverify_contract_kindt::NOT_A_CONTRACT;

  const std::string after_prefix = id_str.substr(jverify_prefix.size());
  const auto colon_pos = after_prefix.find(':');
  const std::string method_name =
    (colon_pos != std::string::npos)
      ? after_prefix.substr(0, colon_pos)
      : after_prefix;

  if(method_name == "precondition")
    return jverify_contract_kindt::PRECONDITION;
  if(method_name == "postcondition")
    return jverify_contract_kindt::POSTCONDITION;
  if(method_name == "invariant")
    return jverify_contract_kindt::INVARIANT;
  if(method_name == "assume")
    return jverify_contract_kindt::ASSUME;
  if(method_name == "check")
    return jverify_contract_kindt::CHECK;
  if(method_name == "decreases")
    return jverify_contract_kindt::DECREASES;

  return jverify_contract_kindt::NOT_A_CONTRACT;
}

void lower_jverify_contracts(goto_modelt &goto_model)
{
  for(auto &func_entry : goto_model.goto_functions.function_map)
  {
    auto &body = func_entry.second.body;
    for(auto it = body.instructions.begin(); it != body.instructions.end(); ++it)
    {
      if(!it->is_function_call())
        continue;

      const auto &call_fn = it->call_function();
      if(call_fn.id() != ID_symbol)
        continue;

      const irep_idt &callee_id =
        to_symbol_expr(call_fn).get_identifier();

      const auto kind = classify_jverify_call(callee_id);
      if(kind == jverify_contract_kindt::NOT_A_CONTRACT)
        continue;

      const auto &args = it->call_arguments();

      // For calls with no boolean argument (lambda postconditions, decreases),
      // just remove the call.
      if(args.empty() || kind == jverify_contract_kindt::DECREASES)
      {
        it->turn_into_skip();
        continue;
      }

      exprt condition = args[0];
      // Java boolean is represented as int in bytecode; cast to bool if needed
      if(condition.type().id() != ID_bool)
        condition = notequal_exprt(condition, from_integer(0, condition.type()));
      source_locationt loc = it->source_location();

      switch(kind)
      {
      case jverify_contract_kindt::PRECONDITION:
      case jverify_contract_kindt::ASSUME:
      {
        loc.set_comment(
          kind == jverify_contract_kindt::PRECONDITION
            ? "JVerify precondition"
            : "JVerify assume");
        *it = goto_programt::make_assumption(condition, loc);
        break;
      }
      case jverify_contract_kindt::POSTCONDITION:
      case jverify_contract_kindt::CHECK:
      case jverify_contract_kindt::INVARIANT:
      {
        const char *comment =
          kind == jverify_contract_kindt::POSTCONDITION
            ? "JVerify postcondition"
            : (kind == jverify_contract_kindt::INVARIANT
                 ? "JVerify loop invariant"
                 : "JVerify check");
        loc.set_comment(comment);
        loc.set_property_class(
          kind == jverify_contract_kindt::POSTCONDITION
            ? "postcondition"
            : (kind == jverify_contract_kindt::INVARIANT
                 ? "loop-invariant"
                 : "assertion"));
        *it = goto_programt::make_assertion(condition, loc);
        break;
      }
      case jverify_contract_kindt::DECREASES:
      case jverify_contract_kindt::NOT_A_CONTRACT:
        UNREACHABLE;
      }
    }
  }
}
