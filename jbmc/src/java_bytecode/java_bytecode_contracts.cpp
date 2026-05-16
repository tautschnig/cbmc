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
#include <util/cprover_prefix.h>
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
#include <util/exception_utils.h>
#include <util/options.h>

#include <goto-programs/goto_model.h>

#include <goto-instrument/contracts/contracts.h>
#include <goto-instrument/contracts/dynamic-frames/dfcc.h>
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
  if(method_name == "assigns")
    return jverify_contract_kindt::ASSIGNS;

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

/// F12 trace-back, mixed-predicate reconstructor for compound
/// boolean lambda bodies.
///
/// Handles the general case `A && (B || C)`, `(A || B) && (C || D)`,
/// and other mixed shapes that javac compiles to a single linear
/// block of guards each targeting either an L_TRUE or an L_FALSE
/// label. The flat AND-chain and OR-chain cases handled by
/// `reconstruct_and_chain` are special cases of this.
///
/// CFG layout we recognize, immediately before the call site:
///
///   IF g1 GOTO L_T_or_F_1
///   IF g2 GOTO L_T_or_F_2
///   ...
///   IF gN GOTO L_T_or_F_N
///   ASSIGN tmp := <C_FT>           (fall-through value, 0 or 1)
///   GOTO L_MERGE                   (optional)
///   L_FALSE: ASSIGN tmp := 0
///                                  (or L_TRUE: ASSIGN tmp := 1)
///   L_MERGE: <here>
///
/// Algorithm: walk the guards in source order, tracking the
/// cumulative "path condition" that must hold for execution to
/// reach the current point without already having committed to
/// TRUE or FALSE. Each guard is classified by the value of `tmp`
/// at its jump target (L_TRUE or L_FALSE). A guard targeting
/// L_TRUE contributes a disjunct `path AND guard.condition` to the
/// final predicate; a guard targeting L_FALSE merely refines `path`
/// to `path AND NOT(guard.condition)`. If fall-through reaches
/// L_TRUE, the final `path` is also a disjunct; if fall-through
/// reaches L_FALSE, no fall-through disjunct is added.
///
/// Worked example for `A && (B || C)`:
///
///   IF !A GOTO L_FALSE       (negative, refines path to A)
///   IF B  GOTO L_TRUE        (positive, emits A AND B)
///   IF !C GOTO L_FALSE       (negative, refines path to A AND !B AND C)
///   ASSIGN tmp := 1          (fall-through TRUE; emits A AND !B AND C)
///   GOTO MERGE
///   L_FALSE: ASSIGN tmp := 0
///   MERGE:
///
///   predicate = (A AND B) OR (A AND !B AND C) ≡ A AND (B OR C).
///
/// Returns `nullopt` if the CFG doesn't match this shape (e.g. a
/// guard jumps to a label that isn't either of {L_TRUE, L_FALSE},
/// or the merge layout doesn't match), so the caller can fall back
/// to the simpler reconstructor or to the safer "drop the predicate"
/// path.
static std::optional<exprt> reconstruct_mixed_predicate(
  goto_programt &body,
  goto_programt::const_targett call_it,
  const irep_idt &temp_id)
{
  if(call_it == body.instructions.begin())
    return {};

  auto is_no_op = [](const goto_programt::const_targett &c)
  {
    return c->is_skip() || c->is_decl() || c->is_dead() || c->is_location() ||
           c->is_other();
  };

  // 1. Walk forward from the body start through the guard sequence
  //    (a run of conditional GOTOs interleaved with no-ops) to
  //    find the first assign of `temp_id`. That assign is the
  //    GUARDS' fall-through — the value `temp_id` takes when none
  //    of the guards' jumps fire.
  //
  //    Note: do NOT identify the fall-through by walking BACK from
  //    `call_it` and taking the linearly-preceding assign. In the
  //    mixed-shape layout javac emits, the linear predecessor of
  //    the merge point is the OTHER (jumped-to) assign, not the
  //    fall-through:
  //
  //      IF g1 GOTO L_FALSE
  //      IF g2 GOTO L_TRUE          ← jumps to the assign:=1 below
  //      IF g3 GOTO L_FALSE
  //      L_TRUE: ASSIGN tmp := 1    ← this is the guards' fall-through
  //      GOTO L_MERGE
  //      L_FALSE: ASSIGN tmp := 0   ← linear predecessor of L_MERGE
  //      L_MERGE: ...
  goto_programt::const_targett fallthrough_assign = body.instructions.cend();
  for(auto it = body.instructions.cbegin(); it != body.instructions.cend();
      ++it)
  {
    if(is_no_op(it))
      continue;
    if(it->is_goto())
    {
      if(it->condition().is_true())
      {
        // An unconditional GOTO mid-guard sequence indicates a
        // shape we don't recognize.
        return {};
      }
      continue;
    }
    if(it->is_assign())
    {
      const exprt &lhs = it->assign_lhs();
      if(
        lhs.id() == ID_symbol &&
        to_symbol_expr(lhs).get_identifier() == temp_id)
      {
        fallthrough_assign = it;
        break;
      }
      // Other assigns mid-guards (e.g. to a different temp) mean
      // the shape isn't what we expect.
      return {};
    }
    return {}; // any other instruction kind: bail.
  }
  if(fallthrough_assign == body.instructions.cend())
    return {};

  const exprt &ft_rhs = fallthrough_assign->assign_rhs();
  if(ft_rhs.id() != ID_constant)
    return {};
  const auto ft_int_opt = numeric_cast<mp_integer>(to_constant_expr(ft_rhs));
  if(!ft_int_opt.has_value())
    return {};
  const bool fallthrough_is_true = (*ft_int_opt) == 1;
  if(!fallthrough_is_true && (*ft_int_opt) != 0)
    return {};

  // 2. Find the OTHER branch's assign — `tmp := !C_FT`. There
  //    should be exactly one, reached via the guards' jumps. We
  //    scan forward from after fallthrough_assign for a unique
  //    constant-rhs assign of `temp_id`.
  goto_programt::const_targett other_assign = body.instructions.cend();
  for(auto it = body.instructions.cbegin(); it != body.instructions.cend();
      ++it)
  {
    if(it == fallthrough_assign)
      continue;
    if(!it->is_assign())
      continue;
    const exprt &lhs = it->assign_lhs();
    if(
      lhs.id() != ID_symbol ||
      to_symbol_expr(lhs).get_identifier() != temp_id)
      continue;
    const exprt &rhs = it->assign_rhs();
    if(rhs.id() != ID_constant)
      continue;
    const auto v = numeric_cast<mp_integer>(to_constant_expr(rhs));
    if(!v.has_value())
      continue;
    if(*v == (fallthrough_is_true ? 0 : 1))
    {
      if(other_assign == body.instructions.cend())
      {
        other_assign = it;
      }
      else
      {
        // Multiple assigns of the OTHER value — not the simple
        // shape we recognize.
        return {};
      }
    }
  }
  if(other_assign == body.instructions.cend())
    return {};

  // 3. Walk guards in source order from the body start up to (but
  //    not including) `fallthrough_assign`. Each guard is an IF
  //    whose target is either fallthrough_assign (refines path) or
  //    other_assign (emits/refines depending on direction). Bail if
  //    we encounter any other shape.
  exprt::operandst disjuncts;
  exprt path = true_exprt();

  for(auto it = body.instructions.cbegin(); it != fallthrough_assign; ++it)
  {
    if(is_no_op(it))
      continue;
    if(!it->is_goto())
    {
      // Anything other than a no-op or a guard before the
      // fall-through means this isn't a clean guard sequence.
      return {};
    }
    if(it->condition().is_true())
      return {}; // unconditional GOTO mid-guards: bail
    if(it->targets.size() != 1)
      return {};
    const auto target = it->targets.front();
    const bool targets_other = (target == other_assign);
    bool targets_fallthrough = false;
    if(target == fallthrough_assign)
    {
      targets_fallthrough = true;
    }
    else
    {
      // Some javac-emitted layouts route the guard to a label that
      // immediately precedes `other_assign` (a no-op like a goto
      // location). Walk forward from `target` skipping no-ops to
      // see whether we land on `other_assign` or `fallthrough_assign`.
      auto t = target;
      while(t != body.instructions.cend() &&
            (t->is_skip() || t->is_decl() || t->is_location()) &&
            t != other_assign && t != fallthrough_assign)
        ++t;
      if(t == other_assign)
      {
        // Treat as targets_other.
      }
      else if(t == fallthrough_assign)
      {
        targets_fallthrough = true;
      }
      else
      {
        // Guard jumps to somewhere else — give up.
        return {};
      }
    }

    const exprt &cond = it->condition();
    // "Firing this guard sets tmp to value-at-target."
    bool fired_yields_true;
    if(targets_other)
      fired_yields_true = !fallthrough_is_true;
    else
      fired_yields_true = fallthrough_is_true;
    (void)targets_fallthrough; // currently subsumed by !targets_other

    if(fired_yields_true)
    {
      // Firing → predicate true. Emit `path AND cond`. Refine path
      // by `NOT cond` (continue with the not-fired case).
      disjuncts.push_back(and_exprt(path, cond));
      path = and_exprt(path, not_exprt(cond));
    }
    else
    {
      // Firing → predicate false. Refine path by NOT cond.
      path = and_exprt(path, not_exprt(cond));
    }
  }

  // 4. Fall-through contribution.
  if(fallthrough_is_true)
    disjuncts.push_back(path);

  if(disjuncts.empty())
    return {};

  // Combine and simplify just enough that the resulting expression
  // is readable in dump output. CBMC's downstream simplification
  // will collapse the (true AND x) etc.
  exprt result =
    disjuncts.size() == 1 ? disjuncts.front() : disjunction(disjuncts);
  return result;
}


///
/// javac compiles `precondition(c1 && c2 && ... && cN)` to:
///
///   IF !c1 GOTO L_F
///   IF !c2 GOTO L_F
///   ...
///   IF !cN GOTO L_F
///   ASSIGN $stack_tmp := 1
///   GOTO L_D
///   L_F: ASSIGN $stack_tmp := 0
///   L_D: CALL precondition($stack_tmp)
///
/// where each `IF !ci` is a goto whose condition is the negation of
/// `ci`. The predicate at the call site is `c1 && c2 && ... && cN`.
/// Match this shape and return the conjunction. Returns `nullopt`
/// when the shape doesn't match.
///
/// We also recognize the dual OR-chain pattern: same shape but the
/// "true" assignment sits behind the GOTO target and the "false"
/// assignment is the fallthrough.
///
/// MIXED shapes (e.g. `A && (B || C)`) are recognized too: see
/// `reconstruct_mixed_predicate` below for the general path-condition
/// algorithm. `reconstruct_and_chain` covers only the flat AND-chain
/// case where every guard targets the SAME label; the mixed
/// reconstructor covers the general case at the cost of a slightly
/// more involved CFG analysis. Both functions return `nullopt` on
/// shapes they don't recognize so the caller can fall back to a
/// safer (but less informative) capture.
static std::optional<exprt> reconstruct_and_chain(
  goto_programt &body,
  goto_programt::const_targett call_it,
  const irep_idt &temp_id)
{
  // Walk backwards from `call_it`. Skip irrelevant instructions.
  // Recognize:
  //   T (target of last GOTO):
  //     ASSIGN temp := 0_or_1   (we'll call it `assign_at_target`)
  //     [we are RIGHT after this label]
  //   ...
  //   GOTO T
  //   ASSIGN temp := 1_or_0     (`assign_fallthrough`)
  //   IF !cN GOTO L_F
  //   ...
  //   IF !c1 GOTO L_F           (may be 1 or many)
  //
  // where `L_F` == the target of `assign_at_target`'s preceding
  // label (if any).
  if(call_it == body.instructions.begin())
    return {};

  auto cursor = std::prev(call_it);
  // Skip past leading no-ops (skips, decls, deads) — these are
  // common between the merge assign and the JVerify call when
  // javac/JBMC interleave variable declarations.
  auto is_no_op = [](const goto_programt::const_targett &c)
  {
    return c->is_skip() || c->is_decl() || c->is_dead() || c->is_location() ||
           c->is_other();
  };
  while(is_no_op(cursor) && cursor != body.instructions.begin())
    --cursor;

  if(!cursor->is_assign())
    return {};
  const exprt &final_lhs = cursor->assign_lhs();
  if(final_lhs.id() != ID_symbol)
    return {};
  if(to_symbol_expr(final_lhs).get_identifier() != temp_id)
    return {};
  // The "merge" branch's label-target is the final assign. Note its
  // value (0 or 1) — this tells us which logical operator we have.
  const exprt &merge_value = cursor->assign_rhs();
  if(merge_value.id() != ID_constant)
    return {};
  const auto merge_int_opt =
    numeric_cast<mp_integer>(to_constant_expr(merge_value));
  if(!merge_int_opt.has_value())
    return {};
  const bool merge_is_one = (*merge_int_opt) == 1;
  // The IFs that compute the predicate jump to THIS instruction
  // (the false-path assignment that falls through to the call).
  const auto target_of_ifs = cursor;

  // The merge-assign is a labeled instruction. Walk further back.
  if(cursor == body.instructions.begin())
    return {};
  --cursor;

  // We now expect a GOTO to bypass us to a fallthrough block that
  // assigns the OTHER value (the one whose path we DIDN'T take to
  // get here).
  if(!cursor->is_goto() || !cursor->condition().is_true())
  {
    // Not the simple `goto + fallthrough` pattern. Bail.
    return {};
  }
  if(cursor == body.instructions.begin())
    return {};
  --cursor;

  // The next instruction back should be the OTHER assignment.
  if(!cursor->is_assign())
    return {};
  const exprt &other_lhs = cursor->assign_lhs();
  if(
    other_lhs.id() != ID_symbol ||
    to_symbol_expr(other_lhs).get_identifier() != temp_id)
    return {};
  const exprt &other_value = cursor->assign_rhs();
  if(other_value.id() != ID_constant)
    return {};
  const auto other_int_opt =
    numeric_cast<mp_integer>(to_constant_expr(other_value));
  if(!other_int_opt.has_value())
    return {};
  // The two values must be 0 and 1 in some order.
  if(merge_int_opt.value() == other_int_opt.value())
    return {};
  // For the AND-chain pattern, taking the IF guard goes to the false
  // branch (assign 0). The fallthrough writes 1. So:
  //   if `merge_is_one` is true, then the assignment after the
  //   guard's GOTO target is `:= 1` — meaning the LAST assignment
  //   we see (immediately before the call) is `:= 1` and the IFs'
  //   condition is the negation of the predicate. That's the
  //   AND-chain shape.
  //   If `merge_is_one` is false, it's the OR-chain shape (dual).
  // We collect the IF conditions and combine accordingly.

  // Walk back through a sequence of IFs that branch to the same
  // target — the target is the label of the merge assignment we
  // saved above.
  if(cursor == body.instructions.begin())
    return {};
  --cursor;

  exprt::operandst guards;
  // Walk up to a reasonable cap.
  for(int i = 0; i < 32; ++i)
  {
    // Skip no-ops (skip, decl, dead, location, other).
    while(is_no_op(cursor) && cursor != body.instructions.begin())
      --cursor;
    if(!cursor->is_goto())
      break;
    if(cursor->condition().is_true()) // an unconditional goto isn't a guard
      break;
    // Confirm the goto target is the merge assignment.
    bool targets_match = false;
    for(const auto &t : cursor->targets)
    {
      if(t == target_of_ifs)
        targets_match = true;
    }
    if(!targets_match)
      break;
    guards.push_back(cursor->condition());
    if(cursor == body.instructions.begin())
      break;
    --cursor;
  }

  if(guards.empty())
    return {};

  // Reconstruct the predicate from the IF guards.
  //
  // Case A — merge_is_one (the instruction immediately before the
  // CALL assigns `1`): the IF guards target the "true" block (the
  // pre-merge assign). Each IF firing routes us through that block
  // and tmp ends up at 1 → predicate true. Predicate is the OR of
  // the guards as-is. This is the dual `||` shape.
  //
  // Case B — merge_is_one false (assign `0` immediately before the
  // CALL): the IF guards target the "false" block. Each IF firing
  // routes us to assign 0 → predicate false. tmp = 1 happens only
  // when NONE of the guards fire, so predicate = AND of (NOT guard).
  // This is the `&&` shape javac emits for our typical
  // `precondition(c1 && c2 && ...)`.
  //
  // Reverse `guards` so the first IF (textually) is first in the
  // resulting AND/OR.
  std::reverse(guards.begin(), guards.end());

  if(merge_is_one)
  {
    // OR-chain: predicate = guard1 || guard2 || ... || guardN
    return disjunction(guards);
  }
  else
  {
    // AND-chain: predicate = !guard1 && !guard2 && ... && !guardN
    exprt::operandst components;
    for(const auto &g : guards)
      components.push_back(not_exprt(g));
    return conjunction(components);
  }
}

/// F12 trace-back: walk the GOTO body backwards from `call_it` to
/// resolve a stack-temp argument to its defining expression.
///
/// `expr` is the argument we observed at the JVerify.precondition /
/// .postcondition call site. javac frequently lowers a boolean
/// expression like `n >= 0 && n <= 1000` to:
///
///   ASSIGN $stack_tmp_a := n >= 0
///   ASSIGN $stack_tmp_b := n <= 1000
///   ASSIGN $stack_tmp_c := $stack_tmp_a && $stack_tmp_b
///   CALL JVerify.precondition($stack_tmp_c)
///
/// For F12 we want the c_requires / c_ensures clause to reference
/// the function's formal parameters, not callee-internal stack
/// temps. This helper unfolds chains of `ASSIGN tmp := <rhs>`
/// instructions until the expression no longer contains any
/// reference to stack-local symbols of `function_id`. Returns the
/// rewritten expression.
///
/// We bound the unfolding by:
///   - depth 32 (the deepest chain we'd realistically expect from
///     javac);
///   - a per-call cache so the same temp isn't re-resolved.
///
/// On failure (e.g. the temp is read before it's first written, or
/// it's assigned from another function's #return_value), we return
/// the original expression and let downstream code do its best.
static exprt resolve_stack_temps(
  const exprt &expr,
  goto_programt &body,
  goto_programt::const_targett call_it,
  const irep_idt &function_id)
{
  const std::string func_prefix = id2string(function_id) + "::";

  std::map<irep_idt, exprt> resolved_cache;

  // Walk back from `call_it` to find the most recent ASSIGN to
  // `target_id`, but bail out if the value is branch-dependent.
  // Specifically: if we cross any GOTO target (label) or any
  // conditional/unconditional branch between the call site and the
  // assignment, the value at the call site might come from a
  // different path. Returning the wrong assignment would silently
  // corrupt the captured predicate.
  auto find_definition = [&](const irep_idt &target_id) -> std::optional<exprt>
  {
    if(call_it == body.instructions.begin())
      return {};
    auto it = std::prev(call_it);
    bool crossed_branch = false;
    while(true)
    {
      // Any incoming or outgoing branch on the path back means the
      // ASSIGN we'd find isn't the unique definition reaching the
      // call site.
      if(it->is_goto() || it->is_target())
        crossed_branch = true;
      if(it->is_assign())
      {
        const exprt &lhs = it->assign_lhs();
        if(
          lhs.id() == ID_symbol &&
          to_symbol_expr(lhs).get_identifier() == target_id)
        {
          if(crossed_branch)
            return {};
          return it->assign_rhs();
        }
      }
      if(it == body.instructions.begin())
        return {};
      --it;
    }
  };

  std::function<exprt(const exprt &, int)> rewrite =
    [&](const exprt &e, int depth) -> exprt
  {
    if(depth > 32)
      return e;
    // Only chase symbols that look like stack locals of this function.
    if(e.id() == ID_symbol)
    {
      const irep_idt &id = to_symbol_expr(e).get_identifier();
      const std::string s = id2string(id);
      const bool is_stack_temp = has_prefix(s, func_prefix) &&
                                 (s.find("$stack_tmp") != std::string::npos ||
                                  s.find("$tmp") != std::string::npos ||
                                  s.find("::tmp") != std::string::npos);
      if(!is_stack_temp)
        return e;
      auto cached = resolved_cache.find(id);
      if(cached != resolved_cache.end())
        return cached->second;
      // First try the mixed-predicate reconstructor (handles
      // compound shapes like `A && (B || C)`). Fall back to the
      // flat AND-chain / OR-chain recognizer if the mixed
      // reconstructor doesn't match.
      auto mixed_rec = reconstruct_mixed_predicate(body, call_it, id);
      if(mixed_rec.has_value())
      {
        exprt unfolded = rewrite(*mixed_rec, depth + 1);
        resolved_cache[id] = unfolded;
        return unfolded;
      }
      auto chain_rec = reconstruct_and_chain(body, call_it, id);
      if(chain_rec.has_value())
      {
        exprt unfolded = rewrite(*chain_rec, depth + 1);
        resolved_cache[id] = unfolded;
        return unfolded;
      }
      const auto def = find_definition(id);
      if(!def.has_value())
        return e;
      exprt unfolded = rewrite(*def, depth + 1);
      resolved_cache[id] = unfolded;
      return unfolded;
    }
    // Recurse into operands.
    exprt result = e;
    for(auto &op : result.operands())
      op = rewrite(op, depth + 1);
    return result;
  };

  return rewrite(expr, 0);
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
  std::map<irep_idt, exprt::operandst> assigns_per_function;
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

      if(kind == jverify_contract_kindt::ASSIGNS)
      {
        // F12: capture user-supplied frame targets. javac wraps the
        // varargs `Object...` into an implicit `Object[]`. We
        // recognise both the direct-argument form (single non-array
        // arg, no varargs wrapping) AND the varargs form: walk back
        // from the CALL collecting slot-stores
        // `*(*(<array>, ...).data + i) := <stored_value>`, then
        // trace each `<stored_value>` to its source lvalue (skipping
        // autoboxing helpers like `Integer.valueOf`).
        //
        // For each `arg` in the call's argument list:
        //   - If arg is NOT a Java array pointer: it is a directly
        //     passed target. Capture as-is.
        //   - Otherwise it's the implicit `Object[]` newarray. Walk
        //     back through the body to find every
        //     `ASSIGN *(*(arg).data + i) := cast(<x>, empty*)`,
        //     unwrap autoboxing, and capture the underlying lvalue.
        for(const auto &arg : args)
        {
          bool is_varargs_array = false;
          if(arg.type().id() == ID_pointer)
          {
            const typet &base = to_pointer_type(arg.type()).base_type();
            if(base.id() == ID_struct_tag)
            {
              const irep_idt id = to_struct_tag_type(base).get_identifier();
              const std::string s = id2string(id);
              if(
                s.find("array[") == 0 ||
                s.find("java::array[") != std::string::npos)
              {
                is_varargs_array = true;
              }
            }
          }
          if(!is_varargs_array)
          {
            assigns_per_function[func_entry.first].push_back(arg);
            continue;
          }

          // Varargs Object[]. Walk back from the call site
          // collecting slot-stores into this specific array.
          if(arg.id() != ID_symbol)
            continue;
          const irep_idt array_id = to_symbol_expr(arg).get_identifier();

          std::function<bool(const exprt &)> references_array =
            [&](const exprt &e) -> bool
          {
            if(e.id() == ID_symbol)
              return to_symbol_expr(e).get_identifier() == array_id;
            for(const auto &op : e.operands())
              if(references_array(op))
                return true;
            return false;
          };

          auto strip_casts = [](exprt e) -> exprt
          {
            while(e.id() == ID_typecast && e.operands().size() == 1)
              e = e.operands()[0];
            return e;
          };

          // Walk back from `from` looking for the most recent
          // ASSIGN to symbol named `sym_id`. Returns nullopt if
          // we cross a branch (so the assignment may not reach
          // `from`).
          auto find_unique_def =
            [&](goto_programt::const_targett from,
                const irep_idt &sym_id) -> std::optional<exprt>
          {
            if(from == body.instructions.begin())
              return {};
            auto cur = std::prev(from);
            bool crossed_branch = false;
            while(true)
            {
              if(cur->is_goto() || cur->is_target())
                crossed_branch = true;
              if(cur->is_assign())
              {
                const exprt &lhs = cur->assign_lhs();
                if(
                  lhs.id() == ID_symbol &&
                  to_symbol_expr(lhs).get_identifier() == sym_id)
                {
                  if(crossed_branch)
                    return {};
                  return cur->assign_rhs();
                }
              }
              if(cur == body.instructions.begin())
                return {};
              --cur;
            }
          };

          // Trace `e_in` to a source lvalue, skipping autoboxing
          // wrappers (Integer.valueOf, Long.valueOf, ...).
          std::function<exprt(const exprt &, goto_programt::const_targett)>
            trace_lvalue =
              [&](const exprt &e_in,
                  goto_programt::const_targett from) -> exprt
          {
            exprt e = strip_casts(e_in);
            if(e.id() != ID_symbol)
              return e;
            const irep_idt sym_id = to_symbol_expr(e).get_identifier();
            const std::string s = id2string(sym_id);
            // Static / instance field (not a stack temp) IS the
            // lvalue.
            if(
              s.find("$tmp") == std::string::npos &&
              s.find("$stack_tmp") == std::string::npos &&
              s.find("::tmp") == std::string::npos &&
              s.find("::return_tmp") == std::string::npos &&
              s.find("#return_value") == std::string::npos)
            {
              return e;
            }
            auto def = find_unique_def(from, sym_id);
            if(!def.has_value())
              return e;
            exprt rhs = strip_casts(*def);
            // Autoboxing wrappers: tmp := <Boxed>.valueOf:(P)L...;
            // #return_value. Find the preceding CALL and use its
            // first argument.
            if(rhs.id() == ID_symbol)
            {
              const std::string rhs_id =
                id2string(to_symbol_expr(rhs).get_identifier());
              if(rhs_id.find("#return_value") != std::string::npos)
              {
                auto cur2 = from;
                if(cur2 == body.instructions.begin())
                  return e;
                while(cur2 != body.instructions.begin())
                {
                  --cur2;
                  if(!cur2->is_function_call())
                    continue;
                  const exprt &fn = cur2->call_function();
                  if(fn.id() != ID_symbol)
                    continue;
                  const std::string fn_id =
                    id2string(to_symbol_expr(fn).get_identifier());
                  bool is_box =
                    fn_id.find(".valueOf:(") != std::string::npos &&
                    fn_id.find("java::java.lang.") != std::string::npos;
                  if(!is_box)
                    continue;
                  const auto &call_args = cur2->call_arguments();
                  if(call_args.empty())
                    return e;
                  return trace_lvalue(call_args.front(), cur2);
                }
                return e;
              }
            }
            return trace_lvalue(rhs, from);
          };

          // Walk back through the body looking for slot-stores
          // into our array.
          if(it == body.instructions.begin())
            continue;
          auto cur = std::prev(it);
          std::map<mp_integer, exprt> captured_by_index;
          while(true)
          {
            if(cur->is_assign())
            {
              const exprt &lhs = cur->assign_lhs();
              if(lhs.id() == ID_dereference && lhs.operands().size() == 1)
              {
                const exprt &addr = lhs.operands()[0];
                if(
                  addr.id() == ID_plus && addr.operands().size() == 2 &&
                  references_array(addr.operands()[0]) &&
                  addr.operands()[1].id() == ID_constant)
                {
                  const auto idx_opt = numeric_cast<mp_integer>(
                    to_constant_expr(addr.operands()[1]));
                  if(idx_opt.has_value())
                  {
                    const exprt traced =
                      trace_lvalue(cur->assign_rhs(), cur);
                    captured_by_index.emplace(*idx_opt, traced);
                  }
                }
              }
            }
            if(cur == body.instructions.begin())
              break;
            --cur;
          }
          for(auto &p : captured_by_index)
            assigns_per_function[func_entry.first].push_back(p.second);
        }
        annotated_functions.insert(func_entry.first);
        it->turn_into_skip();
        continue;
      }

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

        // F12 (lambda-form ensures): inline the lambda target's body
        // into a c_ensures predicate. The lambda target is a small
        // synthetic method whose signature is
        //   (capture_0, ..., capture_{M-1}, return_value) -> bool
        // and whose body is `return <predicate>` for the user-written
        // postcondition lambda. By extracting the predicate and
        // substituting:
        //   target_param_i → info.captures[i]   (for i < M)
        //   target_param_M → __CPROVER_return_value placeholder
        // we recover an expression that references the enclosing
        // function's parameters and __CPROVER_return_value, exactly
        // what CBMC's c_ensures lambda expects.
        //
        // If we can't find a unique return-value assignment in the
        // lambda body, fall back to the existing inline-only path
        // (don't add to annotated_functions).
        {
          const auto target_fn_it =
            goto_model.goto_functions.function_map.find(info.target_method_id);
          if(target_fn_it != goto_model.goto_functions.function_map.end())
          {
            goto_programt &target_body =
              const_cast<goto_programt &>(target_fn_it->second.body);
            const auto target_returns =
              find_return_value_assignments(target_body, info.target_method_id);
            // Resolve the predicate the lambda body returns. Each
            // return path may return a different expression; we only
            // capture if there's exactly one.
            std::optional<exprt> predicate;
            if(target_returns.size() == 1)
            {
              const exprt rhs = target_returns[0]->assign_rhs();
              // The lambda body's expression may itself reference
              // the lambda target's own stack temps. Apply the
              // trace-back so the final expression references only
              // parameters of the lambda target.
              predicate = resolve_stack_temps(
                rhs, target_body, target_returns[0], info.target_method_id);
            }
            else if(target_returns.size() == 2)
            {
              // Block-bodied lambda compiled to dual IRETURNs:
              //   IF cond GOTO L1
              //   ASSIGN #return_value := <const A>   // fallthrough
              //   GOTO END
              //   L1: ASSIGN #return_value := <const B>
              //   END: END_FUNCTION
              // The function returns const_B iff cond is true. If
              // both consts are 0/1 and there's exactly one IF
              // guarding them, the predicate is `cond` (when
              // const_B == 1) or `!cond` (when const_B == 0).
              // We accept either order (which return is first in
              // body iteration order doesn't matter for our
              // pattern recognition).
              const exprt rhs0 = target_returns[0]->assign_rhs();
              const exprt rhs1 = target_returns[1]->assign_rhs();
              auto unwrap_const = [](const exprt &e) -> std::optional<int>
              {
                exprt v = e;
                while(v.id() == ID_typecast && v.operands().size() == 1)
                  v = v.operands()[0];
                if(v.id() != ID_constant)
                  return {};
                const auto i = numeric_cast<mp_integer>(to_constant_expr(v));
                if(!i.has_value())
                  return {};
                if(*i == 0)
                  return 0;
                if(*i == 1)
                  return 1;
                return {};
              };
              const auto v0 = unwrap_const(rhs0);
              const auto v1 = unwrap_const(rhs1);
              if(v0.has_value() && v1.has_value() && *v0 != *v1)
              {
                // Find the IF whose target is the target_return
                // assignment that produces 1.
                const auto truth_target =
                  (*v0 == 1) ? target_returns[0] : target_returns[1];
                const auto false_target =
                  (*v0 == 0) ? target_returns[0] : target_returns[1];
                // Walk the body looking for an IF whose targets
                // include either truth_target or false_target.
                std::optional<exprt> guard;
                bool guard_targets_truth = false;
                for(auto it_l = target_body.instructions.cbegin();
                    it_l != target_body.instructions.cend();
                    ++it_l)
                {
                  if(!it_l->is_goto() || it_l->condition().is_true())
                    continue;
                  for(const auto &t : it_l->targets)
                  {
                    if(t == truth_target)
                    {
                      guard = it_l->condition();
                      guard_targets_truth = true;
                    }
                    else if(t == false_target)
                    {
                      guard = it_l->condition();
                      guard_targets_truth = false;
                    }
                  }
                  if(guard.has_value())
                    break;
                }
                if(guard.has_value())
                {
                  predicate =
                    guard_targets_truth ? *guard : exprt(not_exprt(*guard));
                }
              }
            }
            if(predicate.has_value())
            {
              // Substitute lambda-target parameters with the user's
              // captures + __CPROVER_return_value. Walk the predicate
              // recursively, replacing every symbol_exprt whose
              // identifier matches a lambda-target parameter with
              // the corresponding new expression.
              const auto &target_params = target_type.parameters();
              const auto &enclosing_type = to_code_type(
                goto_model.symbol_table.lookup_ref(func_entry.first).type);
              const typet &enclosing_ret_type = enclosing_type.return_type();
              if(enclosing_ret_type.id() != ID_empty && !target_params.empty())
              {
                const symbol_exprt return_value_sym(
                  CPROVER_PREFIX "return_value", enclosing_ret_type);
                std::map<irep_idt, exprt> subst;
                for(std::size_t i = 0; i < target_params.size(); ++i)
                {
                  const irep_idt &pid = target_params[i].get_identifier();
                  if(pid.empty())
                    continue;
                  exprt replacement;
                  if(i + 1 == target_params.size())
                  {
                    replacement = return_value_sym;
                    if(return_value_sym.type() != target_params[i].type())
                      replacement =
                        typecast_exprt(replacement, target_params[i].type());
                  }
                  else if(i < info.captures.size())
                  {
                    replacement = info.captures[i];
                    if(replacement.type() != target_params[i].type())
                      replacement =
                        typecast_exprt(replacement, target_params[i].type());
                  }
                  else
                  {
                    continue;
                  }
                  subst[pid] = replacement;
                }
                std::function<exprt(const exprt &)> apply_subst =
                  [&](const exprt &e) -> exprt
                {
                  if(e.id() == ID_symbol)
                  {
                    const irep_idt id = to_symbol_expr(e).get_identifier();
                    auto it_subst = subst.find(id);
                    if(it_subst != subst.end())
                      return it_subst->second;
                    return e;
                  }
                  exprt result = e;
                  for(auto &op : result.operands())
                    op = apply_subst(op);
                  return result;
                };
                exprt substituted = apply_subst(*predicate);
                // Coerce to bool.
                if(substituted.type().id() != ID_bool)
                  substituted = notequal_exprt(
                    substituted, from_integer(0, substituted.type()));
                ensures_per_function[func_entry.first].push_back(substituted);
                annotated_functions.insert(func_entry.first);
              }
            }
          }
        }

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
      //
      // javac frequently lowers a non-trivial boolean expression to a
      // chain of stack-temp assignments before the JVerify call:
      //   ASSIGN $tmp_a := n >= 0
      //   ASSIGN $tmp_b := n <= 1000
      //   ASSIGN $tmp_c := $tmp_a && $tmp_b
      //   CALL precondition($tmp_c)
      // For modular substitution we want the predicate to reference
      // the function's formal parameters, not those callee-internal
      // temps. resolve_stack_temps walks the assignment chain back
      // and unfolds the temps in place.
      const exprt resolved_condition =
        resolve_stack_temps(condition, body, it, func_entry.first);
      switch(kind)
      {
      case jverify_contract_kindt::PRECONDITION:
        requires_per_function[func_entry.first].push_back(resolved_condition);
        annotated_functions.insert(func_entry.first);
        break;
      case jverify_contract_kindt::POSTCONDITION:
        ensures_per_function[func_entry.first].push_back(resolved_condition);
        annotated_functions.insert(func_entry.first);
        break;
      case jverify_contract_kindt::INVARIANT:
      case jverify_contract_kindt::ASSUME:
      case jverify_contract_kindt::CHECK:
      case jverify_contract_kindt::DECREASES:
      case jverify_contract_kindt::ASSIGNS:
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
      case jverify_contract_kindt::ASSIGNS:
      case jverify_contract_kindt::NOT_A_CONTRACT:
        UNREACHABLE;
      }
    }
  }

  // F12: only functions with BOTH captured requires AND captured
  // ensures are sound candidates for call-site substitution. A
  // function with requires but no captured ensures (e.g., one whose
  // postcondition is only in lambda form, which we don't yet capture
  // as a c_ensures) would substitute as `assert(req); havoc;
  // assume(true)` — sound, but strictly less informative than
  // inlining, so the caller's proof regresses. Filter such functions
  // out of the substitution set; they still get verified end-to-end
  // via inlining.
  std::set<irep_idt> substitutable_functions;
  for(const auto &fid : annotated_functions)
  {
    bool has_requires = requires_per_function.count(fid) > 0 &&
                        !requires_per_function.at(fid).empty();
    bool has_ensures = ensures_per_function.count(fid) > 0 &&
                       !ensures_per_function.at(fid).empty();
    if(has_requires && has_ensures)
      substitutable_functions.insert(fid);
  }

  // F12: populate `code_with_contract_typet` clauses on every
  // annotated function symbol AND emit a parallel `contract::<fid>`
  // symbol that CBMC's `code_contractst::replace_calls` looks up
  // first. The clauses must be wrapped in `lambda_exprt` over the
  // function's parameter symbols — that's the shape CBMC's
  // contracts machinery expects (see c_typecheck_base.cpp:929).
  for(const auto &fid : substitutable_functions)
  {
    auto sym_it = goto_model.symbol_table.get_writeable(fid);
    if(sym_it == nullptr)
      continue;
    const code_typet &existing_type = to_code_type(sym_it->type);

    // Build the parameter-symbol vector for the lambda binding.
    // CBMC's contract substitution prepends `__CPROVER_return_value`
    // to the call-site's value list when the function has a
    // non-empty return type, so the lambda must bind that variable
    // before the actual parameters. See ansi-c/c_typecheck_base.cpp
    // around line 893 for the C front-end's parallel logic.
    std::vector<symbol_exprt> parameter_syms;
    const typet &return_type = existing_type.return_type();
    if(return_type.id() != ID_empty)
    {
      parameter_syms.emplace_back(CPROVER_PREFIX "return_value", return_type);
    }
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
    auto &c_asg = contract_type.c_assigns();
    auto req_it = requires_per_function.find(fid);
    if(req_it != requires_per_function.end())
      for(const auto &e : req_it->second)
        c_req.push_back(wrap_lambda(e));
    auto ens_it = ensures_per_function.find(fid);
    if(ens_it != ensures_per_function.end())
      for(const auto &e : ens_it->second)
        c_ens.push_back(wrap_lambda(e));
    auto asg_it = assigns_per_function.find(fid);
    if(asg_it != assigns_per_function.end())
    {
      // Only populate c_assigns when we have actual targets. An
      // empty c_assigns is interpreted by CBMC as "no writes
      // allowed", which fails on any body that creates a temp
      // (e.g., the Object[] javac generates for varargs). Until we
      // can recover the user's listed targets from the implicit
      // varargs Object[], leaving c_assigns empty preserves the
      // default "havoc everything" semantics — sound but
      // imprecise.
      for(const auto &t : asg_it->second)
        c_asg.push_back(wrap_lambda(t));
    }
    sym_it->type = contract_type;

    // Insert the parallel contract:: symbol if not already present.
    const irep_idt contract_id = "contract::" + id2string(fid);
    if(!goto_model.symbol_table.has_symbol(contract_id))
    {
      symbolt contract;
      contract.name = contract_id;
      contract.base_name = sym_it->base_name;
      contract.pretty_name = sym_it->pretty_name;
      // Deliberately NOT setting is_property=true. With it set,
      // CBMC's contracts machinery enforces the function (wraps
      // the body to assume requires + assert ensures) which can
      // collide with our existing in-body lowering.
      contract.is_property = false;
      contract.type = contract_type;
      contract.mode = sym_it->mode;
      contract.module = sym_it->module;
      contract.location = sym_it->location;
      goto_model.symbol_table.insert(std::move(contract));
    }
  }

  return substitutable_functions;
}

void apply_modular_contract_substitution(
  goto_modelt &goto_model,
  const std::set<irep_idt> &annotated)
{
  if(annotated.empty())
    return;

  console_message_handlert mh;
  mh.set_verbosity(messaget::M_STATUS);
  messaget log{mh};

  // F12: use DFCC (dynamic frame condition checking) instead of the
  // legacy code_contractst::replace_calls. DFCC's assignable-target
  // codegen at dfcc_contract_clauses_codegen.cpp:143 raises a
  // recoverable invalid_source_file_exceptiont on unsupported lvalue
  // shapes, in contrast to the legacy
  // instrument_spec_assigns.cpp:615 path's UNREACHABLE invariant.
  // DFCC is also the actively-developed contracts implementation.
  //
  // Inputs:
  //   - harness_id: JBMC's standard harness function name.
  //   - to_check: optional. We don't pass one because our F12 flow
  //     doesn't enforce any particular function — we just substitute
  //     calls.
  //   - to_replace: the substitutable functions.
  //   - to_exclude_from_nondet_static: empty by default.

  optionst options;
  // DFCC reads a few options to redefine the entry point. JBMC's
  // entry is __CPROVER__start, which DFCC defaults to when these
  // are absent.

  loop_contract_configt no_loop_contracts;
  std::set<std::string> nondet_static_exclude;
  const irep_idt harness_id{"__CPROVER__start"};

  cbmc_invariants_should_throwt invariants_throw;
  // DFCC mutates goto_model destructively. If it fails partway
  // through (typically because of a Java-mode integration gap with
  // C-builtin helpers it expects), the model is left corrupted and
  // downstream symex hits invariants. Snapshot the model and
  // restore on any failure so the legacy inline path can take over
  // cleanly.
  goto_modelt model_snapshot;
  model_snapshot.symbol_table = goto_model.symbol_table;
  model_snapshot.goto_functions.copy_from(goto_model.goto_functions);

  bool dfcc_succeeded = false;
  try
  {
    dfcc(
      options,
      goto_model,
      harness_id,
      std::optional<irep_idt>{}, // no enforce-contract function
      false,                     // no recursive
      annotated,
      no_loop_contracts,
      nondet_static_exclude,
      mh);
    dfcc_succeeded = true;
  }
  catch(const invalid_source_file_exceptiont &e)
  {
    log.warning() << "F12: DFCC rejected an unsupported lvalue (" << e.what()
                  << "); falling back to inlining" << messaget::eom;
  }
  catch(const invariant_failedt &e)
  {
    log.warning() << "F12: DFCC hit CBMC invariant (" << e.what()
                  << "); falling back to inlining" << messaget::eom;
  }
  catch(const std::out_of_range &e)
  {
    log.warning() << "F12: DFCC failed (out_of_range: " << e.what()
                  << "); falling back to inlining. This typically indicates a "
                  << "missing C-builtin helper symbol (`malloc`, "
                  << "`__CPROVER_assignable`, etc.) — Java-mode integration of "
                  << "those helpers is future work." << messaget::eom;
  }
  catch(const std::exception &e)
  {
    log.warning() << "F12: DFCC failed (" << typeid(e).name() << ": "
                  << e.what() << "); falling back to inlining" << messaget::eom;
  }

  if(!dfcc_succeeded)
  {
    // Restore the pre-DFCC model. Any partial mutation DFCC did is
    // discarded; the existing in-body assume/assert lowering still
    // produces a sound (if non-modular) verification.
    goto_model.symbol_table.clear();
    for(const auto &p : model_snapshot.symbol_table.symbols)
      goto_model.symbol_table.insert(p.second);
    goto_model.goto_functions.clear();
    goto_model.goto_functions.copy_from(model_snapshot.goto_functions);
    return;
  }

  // F12: bridge DFCC's C-style return convention to JBMC's
  // Java-style one.
  //
  // DFCC's wrapper for a function with a non-void return type emits
  //
  //   ASSIGN __contract_return_value := nondet
  //   ASSUME post(__contract_return_value)
  //   SET_RETURN_VALUE __contract_return_value
  //
  // CBMC's symex implements SET_RETURN_VALUE by assigning to the
  // caller frame's `return_value_symbol`, which is only set up when
  // the CALL has an `lhs`. JBMC's bytecode→GOTO lowering follows
  // Java's calling convention instead: the callee assigns its
  // return value into a per-function GLOBAL named
  // `<function_id>#return_value`, the caller does a *bare* CALL
  // (no lhs) followed by an explicit `ASSIGN tmp :=
  // <function_id>#return_value` to read it. With this convention
  // SET_RETURN_VALUE is a no-op (no caller-side return_value_symbol),
  // so the wrapper's post-assumed return value never makes it back
  // to the caller — substitution silently produces a sound but
  // useless contract (any value, including INT_MIN, can flow
  // through the call).
  //
  // Fix: walk each annotated callee's wrapped body and, immediately
  // before every SET_RETURN_VALUE, inject
  //   ASSIGN <function_id>#return_value := <return_value_expr>
  // so the JVerify-style read on the caller side picks up the
  // post-constrained value.
  for(const auto &fid : annotated)
  {
    auto fn_it = goto_model.goto_functions.function_map.find(fid);
    if(fn_it == goto_model.goto_functions.function_map.end())
      continue;

    // The Java-side return_value symbol pattern: <fid>#return_value.
    // It exists for every Java method with a non-void return type.
    const irep_idt return_value_id =
      id2string(fid) + "#return_value";
    const auto *rv_sym = goto_model.symbol_table.lookup(return_value_id);
    if(rv_sym == nullptr)
      continue;  // void-returning method; nothing to bridge.
    const symbol_exprt rv_expr = rv_sym->symbol_expr();

    auto &body = fn_it->second.body;
    for(auto inst_it = body.instructions.begin();
        inst_it != body.instructions.end();
        ++inst_it)
    {
      if(inst_it->type() != SET_RETURN_VALUE)
        continue;
      const exprt rv = inst_it->return_value();
      // Inject ASSIGN <fid>#return_value := rv right before the
      // SET_RETURN_VALUE so JBMC's caller-side read picks up the
      // post-constrained value.
      body.insert_before(
        inst_it,
        goto_programt::make_assignment(
          rv_expr, rv, inst_it->source_location()));
    }
    body.update();
  }
  goto_model.goto_functions.update();
}
