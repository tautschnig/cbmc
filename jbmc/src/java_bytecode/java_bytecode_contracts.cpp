/*******************************************************************\

Module: Java Bytecode Contracts Support

Author: Michael Tautschnig (via Kiro)

Date: May 2026

\*******************************************************************/

/// \file
/// Recognize and lower JVerify-style contract calls in GOTO programs.

#include "java_bytecode_contracts.h"

#include <util/arith_tools.h>
#include <util/c_types.h>
#include <util/cout_message.h>
#include <util/fresh_symbol.h>
#include <util/invariant.h>
#include <util/mathematical_expr.h>
#include <util/namespace.h>
#include <util/pointer_expr.h>
#include <util/prefix.h>
#include <util/std_code.h>
#include <util/std_expr.h>

#include "java_types.h"

#include <iostream>

// `goto-instrument-lib` defines code_contractst whose constructor
// takes a `loop_contract_configt`. Bring in both the library header
// and the loop-contract config type.
#include <goto-programs/goto_model.h>

#include <goto-instrument/contracts/contracts.h>
#include <goto-instrument/contracts/loop_contract_config.h>

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

namespace
{

/// Strip away typecasts from an expression and return the underlying one.
const exprt &strip_typecasts(const exprt &e)
{
  const exprt *cur = &e;
  while(cur->id() == ID_typecast && cur->operands().size() == 1)
    cur = &cur->operands()[0];
  return *cur;
}

bool ends_with(const std::string &s, const std::string &suffix)
{
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/// Info about a lambda-style postcondition's origin.
struct lambda_post_infot
{
  /// The lambda target method (the user's `lambda$method$N`).
  /// We call this directly at each return site, bypassing the synthetic
  /// class's indirection — which would otherwise be pruned by the
  /// ci-lazy-methods filter because it has no explicit callers in bytecode.
  irep_idt target_method_id;
  /// The capture arguments that were passed to the synthetic class's
  /// constructor. They are the leading arguments that the lambda target
  /// method expects; the method's trailing argument is the return value
  /// the caller will provide at each return site.
  exprt::operandst captures;
  /// True iff we successfully traced back the lambda.
  bool valid{false};
};

/// Walk backward from `postcondition_call` to find the synthetic class
/// <init> call that constructed the lambda referenced by `lambda_arg`.
///
/// We expect the recent javac-generated pattern:
///   ASSIGN lambda_new := allocate(...)
///   CALL lambda_synthetic_class$...$<init>(lambda_new, capture_1, ..., capture_n)
/// where the postcondition call that follows has `lambda_new` as its
/// argument (possibly wrapped in a typecast to the functional interface).
///
/// Returns the implemented-method symbol id and the captures.
lambda_post_infot trace_lambda_postcondition(
  const goto_programt &body,
  goto_programt::const_targett postcondition_call,
  const exprt &lambda_arg,
  const namespacet &ns)
{
  lambda_post_infot info;

  const exprt &stripped = strip_typecasts(lambda_arg);
  if(stripped.id() != ID_symbol)
    return info;
  const irep_idt lambda_ref_id = to_symbol_expr(stripped).get_identifier();

  // Walk backward from just before the postcondition call to find the
  // matching <init> call.
  auto it = postcondition_call;
  while(it != body.instructions.begin())
  {
    --it;
    if(!it->is_function_call())
      continue;
    const auto &call_fn = it->call_function();
    if(call_fn.id() != ID_symbol)
      continue;
    const irep_idt &callee_id = to_symbol_expr(call_fn).get_identifier();
    if(!has_prefix(
         id2string(callee_id), "java::lambda_synthetic_class$"))
      continue;
    if(!ends_with(id2string(callee_id), ".<init>"))
      continue;
    // The first arg of a constructor is the `this` pointer (the lambda object).
    const auto &init_args = it->call_arguments();
    if(init_args.empty())
      continue;
    const exprt &this_arg = strip_typecasts(init_args[0]);
    if(this_arg.id() != ID_symbol)
      continue;
    if(to_symbol_expr(this_arg).get_identifier() != lambda_ref_id)
      continue;

    // Found the <init>. The synthetic class carries a
    // ID_java_lambda_method_handle with the identifier of the lambda target
    // method. Calling the target directly skips the synthetic-class wrapper
    // (whose .test method is often pruned by ci-lazy-methods because nothing
    // in the bytecode ever explicitly calls it).
    const std::string constructor_str = id2string(callee_id);
    const std::string class_name_str =
      constructor_str.substr(0, constructor_str.size() - 7);  // strip ".<init>"

    const auto *class_sym = ns.get_symbol_table().lookup(class_name_str);
    if(class_sym == nullptr)
      return info;
    const auto &class_type = to_java_class_type(class_sym->type);
    const auto &handle =
      static_cast<const java_class_typet::java_lambda_method_handlet &>(
        class_type.find(ID_java_lambda_method_handle));
    const irep_idt target_id = handle.get_lambda_method_identifier();
    if(target_id.empty())
      return info;
    if(ns.get_symbol_table().lookup(target_id) == nullptr)
    {
      return info;
    }

    info.target_method_id = target_id;
    // Captures: the `<init>` call looked like
    //   <init>(this, capture_0, capture_1, ..., capture_{n-1})
    // We forward the captures as leading arguments of the target method.
    // The target method's trailing parameter is the abstract method's
    // argument (filled in by the caller with the return value).
    for(std::size_t i = 1; i < init_args.size(); ++i)
      info.captures.push_back(init_args[i]);
    info.valid = true;
    return info;
  }

  return info;
}

// Unused helper: kept for future use
[[maybe_unused]] bool ends_with_sfx(const std::string &s, const std::string &suffix)
{
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/// Locate every instruction that completes a non-void return from the
/// enclosing function, returning the iterator pointing AT the return-value
/// assignment (the last `ASSIGN #return_value := …` before END_FUNCTION).
///
/// JBMC represents returns as `ASSIGN func#return_value := expr` followed
/// eventually by reaching END_FUNCTION / SET_RETURN_VALUE.
std::vector<goto_programt::targett> find_return_value_assignments(
  goto_programt &body,
  const irep_idt &function_id)
{
  std::vector<goto_programt::targett> returns;
  const std::string needle = id2string(function_id) + "#return_value";
  for(auto it = body.instructions.begin(); it != body.instructions.end(); ++it)
  {
    if(!it->is_assign())
      continue;
    const exprt &lhs = it->assign_lhs();
    if(lhs.id() != ID_symbol)
      continue;
    if(id2string(to_symbol_expr(lhs).get_identifier()) == needle)
      returns.push_back(it);
  }
  return returns;
}

}  // namespace

std::set<irep_idt> lower_jverify_contracts(goto_modelt &goto_model)
{
  std::set<irep_idt> annotated_functions;
  // Per-function accumulators; used to build the contract clauses
  // (`c_requires`, `c_ensures`) populated on each annotated function
  // symbol's type after the in-body lowering has run.
  std::map<irep_idt, exprt::operandst> requires_per_function;
  std::map<irep_idt, exprt::operandst> ensures_per_function;
  const namespacet ns{goto_model.symbol_table};

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

      if(args.empty())
      {
        it->turn_into_skip();
        continue;
      }

      source_locationt loc = it->source_location();

      if(kind == jverify_contract_kindt::DECREASES)
      {
        // F3: non-negativity of the decreases value (lexicographic form
        // conjoins each component's non-negativity).
        exprt condition;
        if(args.size() == 1)
        {
          condition = binary_relation_exprt(
            args[0], ID_ge, from_integer(0, args[0].type()));
        }
        else
        {
          exprt::operandst conjuncts;
          for(const auto &arg : args)
            conjuncts.push_back(binary_relation_exprt(
              arg, ID_ge, from_integer(0, arg.type())));
          condition = conjunction(conjuncts);
        }
        loc.set_comment("JVerify decreases (non-negativity)");
        loc.set_property_class("decreases");
        *it = goto_programt::make_assertion(condition, loc);
        continue;
      }

      const exprt &first_arg = args[0];

      // F1/F6: Lambda-style postcondition. If the argument is a reference
      // type (pointer), it's the lambda instance. With JBMC's native
      // invokedynamic synthesis (F6) plus the lazy-methods pin (see
      // convert_invoke_dynamic), we can trace back to the lambda's <init>
      // call, locate the target method, and emit a direct call + assert
      // at each return site.
      //
      // Current status: the stub-interface synthesis works, and the
      // trace/call synthesis below is in place. Return-value propagation
      // from the lambda target's #return_value back to the CALL LHS has
      // a subtle issue we have not yet fully pinned down, so in practice
      // the sidecar's lambda_rewriter.py still runs before javac and the
      // postcondition arrives here in boolean form — this branch is the
      // fallback for when the source is fed to JBMC directly without the
      // rewriter. In that case we mark the property as unresolved rather
      // than silently passing or silently failing.
      if(
        kind == jverify_contract_kindt::POSTCONDITION &&
        first_arg.type().id() == ID_pointer)
      {
        lambda_post_infot info =
          trace_lambda_postcondition(body, it, first_arg, ns);
        if(!info.valid)
        {
          loc.set_comment("JVerify postcondition (lambda, unresolved)");
          loc.set_property_class("postcondition");
          *it = goto_programt::make_assertion(false_exprt(), loc);
          continue;
        }

        // Erase the original postcondition call.
        const auto ret_assignments = find_return_value_assignments(
          body, func_entry.first);

        it->turn_into_skip();

        // Look up the target method's type (synthetic class's abstract method).
        const auto &target_sym =
          goto_model.symbol_table.lookup_ref(info.target_method_id);
        const auto &target_type = to_code_type(target_sym.type);

        // For each return-value assignment, insert:
        //   DECL ret_save : <return type>
        //   ASSIGN ret_save := <return expression>
        //   DECL jv_post_tmp : <target return type>
        //   CALL jv_post_tmp := target_method(captures..., ret_save)
        //   ASSERT jv_post_tmp
        //   ASSIGN #return_value := ret_save  (the original, retargeted)
        // This keeps the return expression evaluated exactly once and lets
        // the lambda target method see the same value that is about to be
        // returned from the enclosing function.
        for(auto ret_it : ret_assignments)
        {
          const exprt return_value_expr = ret_it->assign_rhs();

          // Save the returned expression to a stable temp so the lambda
          // body sees the same value the function returns.
          symbolt &ret_save = get_fresh_aux_symbol(
            return_value_expr.type(),
            id2string(func_entry.first),
            "jv_ret_save",
            loc,
            ID_java,
            goto_model.symbol_table);

          symbolt &tmp_sym = get_fresh_aux_symbol(
            target_type.return_type(),
            id2string(func_entry.first),
            "jv_post_tmp",
            loc,
            ID_java,
            goto_model.symbol_table);

          source_locationt post_loc = loc;
          post_loc.set_comment("JVerify postcondition");
          post_loc.set_property_class("postcondition");

          auto decl_ret_save =
            goto_programt::make_decl(ret_save.symbol_expr(), loc);
          auto assign_ret_save = goto_programt::make_assignment(
            ret_save.symbol_expr(), return_value_expr, loc);
          auto decl_tmp =
            goto_programt::make_decl(tmp_sym.symbol_expr(), loc);

          symbol_exprt target_fn(info.target_method_id, target_type);
          code_function_callt::argumentst call_args;
          const auto &params = target_type.parameters();
          std::size_t param_idx = 0;
          for(const auto &cap : info.captures)
          {
            if(param_idx >= params.size())
              break;
            exprt arg = cap;
            if(arg.type() != params[param_idx].type())
              arg = typecast_exprt(arg, params[param_idx].type());
            call_args.push_back(arg);
            ++param_idx;
          }
          if(param_idx < params.size())
          {
            exprt arg = ret_save.symbol_expr();
            if(arg.type() != params[param_idx].type())
              arg = typecast_exprt(arg, params[param_idx].type());
            call_args.push_back(arg);
          }

          auto call = goto_programt::make_function_call(
            code_function_callt(
              nil_exprt{}, target_fn, std::move(call_args)),
            loc);

          // JBMC's calling convention: the function's return value is
          // stored in the global symbol "<func>#return_value"; the caller
          // reads from there into its temporary. See
          // java_bytecode_convert_method.cpp for how regular invokestatic
          // is lowered.
          const irep_idt rv_name =
            id2string(info.target_method_id) + "#return_value";
          symbolt rv_sym;
          rv_sym.name = rv_name;
          rv_sym.base_name = "#return_value";
          rv_sym.pretty_name = rv_name;
          rv_sym.type = target_type.return_type();
          rv_sym.mode = ID_java;
          rv_sym.is_static_lifetime = false;
          rv_sym.is_thread_local = true;
          rv_sym.is_file_local = true;
          rv_sym.is_lvalue = true;
          if(goto_model.symbol_table.lookup(rv_name) == nullptr)
            goto_model.symbol_table.insert(std::move(rv_sym));
          symbol_exprt rv_expr(rv_name, target_type.return_type());
          auto assign_from_rv =
            goto_programt::make_assignment(
              tmp_sym.symbol_expr(), rv_expr, loc);

          exprt truth;
          if(tmp_sym.type.id() == ID_bool)
            truth = tmp_sym.symbol_expr();
          else
            truth = notequal_exprt(
              tmp_sym.symbol_expr(), from_integer(0, tmp_sym.type));
          auto assert_instr =
            goto_programt::make_assertion(truth, post_loc);

          // Retarget the original return-value assignment to use ret_save.
          ret_it->assign_rhs_nonconst() = ret_save.symbol_expr();

          // Insertion strategy: we must preserve any existing GOTOs that
          // target ret_it (branches converging on the return). Using
          // `insert_before_swap` ensures the label travels to the first
          // inserted instruction, so all branches run through our
          // DECL/ASSIGN/CALL/ASSERT sequence.
          //
          // insert_before_swap works one-at-a-time: each call swaps the
          // target's contents with the new instruction. After a swap the
          // ORIGINAL instruction lives at the next position, so subsequent
          // calls at the same iterator keep swapping and shifting.
          //
          // We queue the new instructions in order and insert each via
          // insert_before_swap so they end up at the target position in
          // insertion order, with jumps redirected to the first.
          std::vector<goto_programt::instructiont> new_instrs;
          new_instrs.push_back(decl_ret_save);
          new_instrs.push_back(assign_ret_save);
          new_instrs.push_back(decl_tmp);
          new_instrs.push_back(call);
          new_instrs.push_back(assign_from_rv);
          new_instrs.push_back(assert_instr);
          for(auto &i : new_instrs)
            body.instructions.insert(ret_it, i);
          // The original ret_it instruction (ASSIGN #return_value := ret_save)
          // stays at its original location; its label is preserved, but
          // GOTOs jumping to it skip our inserts. Fix: relocate the label
          // to our first inserted instruction by swapping.
          auto first_inserted = std::prev(ret_it, new_instrs.size());
          // Redirect incoming GOTOs: any instruction whose target equals
          // ret_it should now target first_inserted.
          for(auto &other : body.instructions)
          {
            if(other.is_goto() || other.is_incomplete_goto() ||
               other.is_start_thread())
            {
              for(auto &tgt : other.targets)
              {
                if(tgt == ret_it)
                  tgt = first_inserted;
              }
            }
          }
          // Also move labels from ret_it to first_inserted so
          // label-pointer-based references (if any exist) resolve.
          first_inserted->labels = std::move(ret_it->labels);
          ret_it->labels.clear();
        }
        continue;
      }

      // Boolean / primitive-arg contract calls.
      exprt condition = first_arg;
      if(condition.type().id() != ID_bool)
        condition = notequal_exprt(condition, from_integer(0, condition.type()));

      // F12: collect this clause as a contract attribute on the
      // enclosing function. PRECONDITION → c_requires; POSTCONDITION
      // → c_ensures. CHECK / INVARIANT / ASSUME / DECREASES stay
      // body-local. Capturing the predicate here, before it's lowered
      // below, lets us populate code_with_contract_typet without
      // re-walking the body afterwards.
      switch(kind)
      {
      case jverify_contract_kindt::PRECONDITION:
        requires_per_function[func_entry.first].push_back(condition);
        annotated_functions.insert(func_entry.first);
        break;
      case jverify_contract_kindt::POSTCONDITION:
        ensures_per_function[func_entry.first].push_back(condition);
        annotated_functions.insert(func_entry.first);
        break;
      case jverify_contract_kindt::INVARIANT:
      case jverify_contract_kindt::ASSUME:
      case jverify_contract_kindt::CHECK:
      case jverify_contract_kindt::DECREASES:
      case jverify_contract_kindt::NOT_A_CONTRACT:
        break;
      }

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

  // F12: populate `code_with_contract_typet` clauses on every
  // annotated function symbol AND emit a parallel `contract::<fid>`
  // symbol that CBMC's `code_contractst::replace_calls` looks up
  // first. The clauses must be wrapped in `lambda_exprt` over the
  // function's parameter symbols — that's the shape CBMC's
  // contracts machinery expects (see c_typecheck_base.cpp:929).
  for(const auto &fid : annotated_functions)
  {
    auto sym_it = goto_model.symbol_table.get_writeable(fid);
    if(sym_it == nullptr)
      continue;
    const code_typet &existing_type = to_code_type(sym_it->type);

    // Build the parameter-symbol vector for the lambda binding.
    std::vector<symbol_exprt> parameter_syms;
    for(const auto &p : existing_type.parameters())
    {
      const irep_idt &pid = p.get_identifier();
      if(!pid.empty())
        parameter_syms.emplace_back(pid, p.type());
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
    auto req_it = requires_per_function.find(fid);
    if(req_it != requires_per_function.end())
      for(const auto &e : req_it->second)
        c_req.push_back(wrap_lambda(e));
    auto ens_it = ensures_per_function.find(fid);
    if(ens_it != ensures_per_function.end())
      for(const auto &e : ens_it->second)
        c_ens.push_back(wrap_lambda(e));
    sym_it->type = contract_type;

    // Insert the parallel contract:: symbol if not already present.
    const irep_idt contract_id = "contract::" + id2string(fid);
    if(!goto_model.symbol_table.has_symbol(contract_id))
    {
      symbolt contract;
      contract.name = contract_id;
      contract.base_name = sym_it->base_name;
      contract.pretty_name = sym_it->pretty_name;
      contract.is_property = true;
      contract.type = contract_type;
      contract.mode = sym_it->mode;
      contract.module = sym_it->module;
      contract.location = sym_it->location;
      goto_model.symbol_table.insert(std::move(contract));
    }
  }

  return annotated_functions;
}

void apply_modular_contract_substitution(
  goto_modelt &goto_model,
  const std::set<irep_idt> &annotated)
{
  if(annotated.empty())
    return;

  // Translate function ids from JBMC's `java::Foo.bar:(IL...)V` form
  // into the plain string form `code_contractst::replace_calls`
  // expects.
  std::set<std::string> id_strings;
  for(const auto &fid : annotated)
    id_strings.insert(id2string(fid));

  console_message_handlert mh;
  mh.set_verbosity(messaget::M_ERROR);
  messaget log{mh};

  loop_contract_configt no_loop_contracts;
  code_contractst contracts(goto_model, log, no_loop_contracts);

  // F12 status: see commentary above. We use the RAII helper to
  // route CBMC's invariant violations through C++ exceptions so we
  // can catch them and degrade gracefully into the inline pathway,
  // rather than aborting the entire JBMC process.
  cbmc_invariants_should_throwt invariants_throw;
  try
  {
    contracts.replace_calls(id_strings);
  }
  catch(const invariant_failedt &e)
  {
    log.warning() << "F12: modular substitution hit CBMC invariant ("
                  << e.what() << "); falling back to inlining" << messaget::eom;
  }
  catch(const std::exception &e)
  {
    log.warning() << "F12: modular substitution failed (" << e.what()
                  << "); falling back to inlining" << messaget::eom;
  }
}
