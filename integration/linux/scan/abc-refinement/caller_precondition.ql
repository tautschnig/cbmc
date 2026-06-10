/**
 * @name Caller-precondition check for a bounded function-granularity hit
 * @description Automates the triage question behind the dominant residual
 *   false-positive class: a function flagged at FUNCTION granularity (it
 *   reads/uses a bound without an in-function check) may be perfectly safe
 *   because every CALLER validates the bound first.  The canonical case is
 *   rxkad_decrypt_ticket -- the flags byte is read with no in-function
 *   guard, but the caller rxkad_verify_response rejects ticket_len < 4.
 *
 *   For a function with a "bound" parameter (a len/size/count integral, or
 *   the size companion of a (ptr,size)/(p,end) cursor pair), this reports,
 *   per call site, whether the bound argument is constrained by a
 *   "validate-then-reject" guard in the caller -- an
 *   `if (V <relop> CONST) { return | goto | break }` that precedes the
 *   call.  Aggregated: callers / guarded-callers / verdict.  A
 *   CALLER-GUARDED verdict means the function-granularity oracle hit is an
 *   FP resolved by the callers.
 * @kind problem
 * @id cpp/abc/caller-precondition
 * @problem.severity warning
 */
import cpp
import semmle.code.cpp.controlflow.Dominance
import semmle.code.cpp.controlflow.SSA
import KernelTaint

/** A "bound" parameter: a len/size/count-named integral parameter. */
predicate boundParam(Function f, Parameter bp) {
  bp.getFunction() = f and
  bp.getUnspecifiedType() instanceof IntegralType and
  bp.getName()
      .toLowerCase()
      .matches(["%len%", "%size%", "%count%", "%\\_nr", "nr\\_%", "%num%"])
}

/** A compile-time constant bound. */
predicate constBound(Expr e) {
  e instanceof Literal or
  e instanceof EnumConstantAccess or
  e instanceof SizeofOperator or
  (exists(e.getValue()) and not e instanceof VariableAccess)
}

/** Holds if `arg` (an actual argument at a call) is a variable that the
 *  caller constrains with a validate-then-reject guard that DOMINATES the
 *  call: `if (V <relop> CONST) { return|goto|break; }` whose condition
 *  strictly dominates `fc` (so every path to the call has passed the
 *  guard, and -- the then-branch being an exit -- the reject did not
 *  fire). */
predicate guardedArg(FunctionCall fc, Expr arg) {
  exists(
    SsaDefinition ssa, StackVariable v, IfStmt ifs, RelationalOperation rel,
    VariableAccess guse, Expr k
  |
    // call argument and guard operand are uses of the SAME SSA definition
    // of v -> v is not reassigned between the guard and the call (no stale
    // guard)
    arg = ssa.getAUse(v) and
    guse = ssa.getAUse(v) and
    ifs.getEnclosingFunction() = fc.getEnclosingFunction() and
    rel = ifs.getCondition().getAChild*() and
    rel.getAnOperand() = guse and
    k = rel.getAnOperand() and
    k != guse and
    constBound(k) and
    // the then-branch is an early exit (reject)
    exists(Stmt jump |
      jump.getParentStmt*() = ifs.getThen() and
      (jump instanceof ReturnStmt or jump instanceof JumpStmt)
    ) and
    // the guard provably dominates the call (control-flow, not line-order)
    strictlyDominates(ifs.getCondition(), fc)
  )
}

/** Number of call sites of `target`. */
int numCallers(Function target) {
  result = count(FunctionCall fc | fc.getTarget() = target)
}

/** Number of call sites where the bound argument is caller-guarded. */
int numGuarded(Function target, Parameter bp) {
  result =
    count(FunctionCall fc |
      fc.getTarget() = target and
      guardedArg(fc, fc.getArgument(bp.getIndex()))
    )
}

/** A `(p, end)` / `(p, limit)` cursor function: a pointer cursor parameter
 *  plus an `end`/`limit` pointer parameter (ceph / XDR / rxrpc style). */
predicate cursorFunction(Function f) {
  exists(Parameter p, Parameter e |
    p.getFunction() = f and e.getFunction() = f and p != e and
    p.getUnspecifiedType() instanceof PointerType and
    e.getUnspecifiedType() instanceof PointerType and
    e.getName().toLowerCase().matches(["%end%", "%limit%"])
  )
}

/** A caller establishes a byte-availability bound before the call: a
 *  ceph_decode_need / ceph_has_room / pskb_may_pull / *_safe check, or a
 *  relational comparison mentioning an `end`/`limit` cursor, located before
 *  the call.  Necessary-not-sufficient: it bounds the FIRST reads, not an
 *  arbitrary multi-field sub-decode. */
predicate cursorCallerGuard(FunctionCall fc) {
  exists(FunctionCall g |
    g.getEnclosingFunction() = fc.getEnclosingFunction() and
    g.getTarget()
        .getName()
        .matches([
            "ceph_decode_need", "ceph_has_room", "pskb_may_pull",
            "skb_header_pointer", "%\\_safe"
          ]) and
    strictlyDominates(g, fc)
  )
  or
  exists(RelationalOperation rel |
    rel.getEnclosingFunction() = fc.getEnclosingFunction() and
    rel.getAnOperand()
        .(VariableAccess)
        .getTarget()
        .getName()
        .toLowerCase()
        .matches(["%end%", "%limit%"]) and
    strictlyDominates(rel, fc)
  )
}

/** Number of call sites where the cursor bound is established by the caller. */
int numGuardedCursor(Function target) {
  result =
    count(FunctionCall fc |
      fc.getTarget() = target and cursorCallerGuard(fc)
    )
}

/** A struct field used in `f` as the offset into a fixed-size array
 *  (`arr[s->fld]`) -- the struct-field index shape (e.g. cgw
 *  `cf->data[crc8->result_idx]`). */
predicate consumesIndexField(Function f, Field fld) {
  exists(ArrayExpr ae |
    ae.getEnclosingFunction() = f and
    ae.getArrayOffset().(FieldAccess).getTarget() = fld and
    exists(ae.getArrayBase().getType().getUnspecifiedType().(ArrayType).getSize())
  )
}

/** The field's enclosing struct is bulk-populated from raw netlink/user
 *  bytes somewhere -- so the field carries attacker-controlled data. */
predicate fieldFromRaw(Field fld) {
  exists(FunctionCall fc, Expr dest |
    fc.getTarget().getName() =
      ["nla_memcpy", "copy_from_user", "__copy_from_user", "memcpy_from_msg"] and
    dest = fc.getArgument(0) and
    fld.getDeclaringType() =
      dest.getType().getUnspecifiedType().(PointerType).getBaseType().getUnspecifiedType()
  )
}

/** The field is validated at the producer: a chk/check/validate/verify call
 *  taking an access to the field, or a relational on the field against a
 *  compile-time bound (e.g. cgw_chk_csum_parms(c->result_idx, ...)). */
predicate fieldValidated(Field fld) {
  exists(FunctionCall v |
    v.getTarget()
        .getName()
        .toLowerCase()
        .matches(["%chk%", "%check%", "%valid%", "%verify%", "%sanit%"]) and
    v.getAnArgument().(FieldAccess).getTarget() = fld
  )
  or
  exists(RelationalOperation rel, Expr k |
    rel.getAnOperand().(FieldAccess).getTarget() = fld and
    k = rel.getAnOperand() and
    k != rel.getAnOperand().(FieldAccess) and
    constBound(k)
  )
}

/** Producer-side verdict for a struct-field bound. */
string producerVerdict(Field fld) {
  if fieldValidated(fld) then result = "PRODUCER-GUARDED" else result = "PRODUCER-UNGUARDED"
}

/** An `struct sk_buff *` parameter. */
predicate skbParam(Function f, Parameter skb) {
  skb.getFunction() = f and
  skb.getType()
      .getUnspecifiedType()
      .(PointerType)
      .getBaseType()
      .getUnspecifiedType()
      .(Struct)
      .getName() = "sk_buff"
}

/** `f` reads `skb->data` (the skb-parser shape the skb oracle flags). */
predicate readsSkbData(Function f) {
  exists(KernelTaint::SkbDataAccess s | s.getEnclosingFunction() = f)
}

/** A call site where the caller establishes the skb length before the
 *  call: a dominating `pskb_may_pull`-family check, or a relational on
 *  `skb->len`, on the same skb passed as the `skb` parameter. */
predicate skbCallerGuard(FunctionCall fc, Parameter skb) {
  exists(Variable v |
    fc.getArgument(skb.getIndex()).(VariableAccess).getTarget() = v
  |
    exists(FunctionCall g |
      g.getEnclosingFunction() = fc.getEnclosingFunction() and
      g.getTarget()
          .getName()
          .matches([
              "pskb_may_pull", "skb_may_pull", "__pskb_pull%",
              "pskb_network_may_pull", "skb_header_pointer"
            ]) and
      g.getAnArgument().(VariableAccess).getTarget() = v and
      strictlyDominates(g, fc)
    )
    or
    exists(RelationalOperation rel, FieldAccess len |
      rel.getEnclosingFunction() = fc.getEnclosingFunction() and
      len = rel.getAnOperand() and
      len.getTarget().getName() = "len" and
      len.getQualifier().(VariableAccess).getTarget() = v and
      strictlyDominates(rel, fc)
    )
  )
}

/** Number of call sites where the skb length is established by the caller. */
int numGuardedSkb(Function target, Parameter skb) {
  result =
    count(FunctionCall fc |
      fc.getTarget() = target and skbCallerGuard(fc, skb)
    )
}

/** The skb passed to call `fc` is forwarded from the caller's own skb
 *  parameter (so the bound can be tracked one frame up). */
predicate skbForwarded(
  FunctionCall fc, Parameter calleeSkb, Function caller, Parameter callerSkb
) {
  caller = fc.getEnclosingFunction() and
  skbParam(caller, callerSkb) and
  fc.getArgument(calleeSkb.getIndex()).(VariableAccess).getTarget() = callerSkb
}

/** A witness that `f` is NOT guarded on all paths: a call chain of length
 *  <= d up which the skb reaches `f` with no ancestor pulling it.
 *  Conservative -- if the skb at a call cannot be traced to a forwarded
 *  caller parameter, the path counts as unpulled (so we never wrongly
 *  certify a function as guarded). */
predicate hasUnpulledPath(Function f, Parameter skb, int d) {
  skbParam(f, skb) and
  d = [0 .. 3] and
  (
    // f is a root: the skb arrives from outside this TU, unpulled here
    not exists(FunctionCall fc | fc.getTarget() = f)
    or
    exists(FunctionCall fc | fc.getTarget() = f and not skbCallerGuard(fc, skb) |
      // skb does not come from the caller's forwarded param -> can't trace
      not skbForwarded(fc, skb, _, _)
      or
      // the caller is itself a root (no further frame to pull in)
      not exists(FunctionCall up | up.getTarget() = fc.getEnclosingFunction())
      or
      // recurse one frame up on the forwarded skb
      d > 0 and
      exists(Function caller, Parameter callerSkb |
        skbForwarded(fc, skb, caller, callerSkb) and
        hasUnpulledPath(caller, callerSkb, d - 1)
      )
    )
  )
}

/** skb-pull verdict, interprocedural to depth 3.  CALLER-GUARDED only when
 *  EVERY path (within depth 3) pulls the skb before reaching `f`. */
string skbVerdict(Function f, Parameter skb) {
  if not hasUnpulledPath(f, skb, 3)
  then result = "CALLER-GUARDED"
  else if numGuardedSkb(f, skb) > 0 then result = "PARTIAL"
  else result = "UNGUARDED"
}

/** Verdict string. */
bindingset[callers, guarded]
string verdict(int callers, int guarded) {
  if callers = guarded
  then result = "CALLER-GUARDED"
  else if guarded > 0 then result = "PARTIAL" else result = "UNGUARDED"
}

//----------------------------------------------------------------------------
// Shape 5: validate-at-storage-then-parse-later (NON-LOCAL validator).
//
// The dominant residual FP after the four local shapes is a function whose
// bytes were length-validated TWO LAYERS UP, at the point the element was
// stashed into an array, not in the immediate caller.  Canonical case:
// mac80211 `ieee80211_get_ttlm` -- parse.c stores a T2L element into
// `elems->ttlm[]` only when `ieee80211_tid_to_link_map_size_ok(data,len)`
// passes; `ieee80211_process_adv_ttlm` later walks `elems->ttlm[i]` ->
// `ieee80211_parse_adv_t2l(.., elems->ttlm[i], ..)` -> `pos =
// ttlm->optional` -> `ieee80211_get_ttlm(map_size, pos)`.  The local
// caller-precondition shapes report UNGUARDED because no guard sits in the
// immediate caller; this shape follows the validator to the storage gate.
//----------------------------------------------------------------------------

/** A struct field used as a "validated storage" slot: some store into it
 *  (`s->F = data` or `s->F[..] = data`) is gated by a `*_ok` / `*_size_ok`
 *  / `*_check` / `*may_pull` validator APPLIED TO THE STORED VALUE, so only
 *  length-validated objects ever land in `F`. */
predicate validatedStorageField(Field arrFld) {
  exists(Assignment a, FieldAccess lhsF, FunctionCall val, IfStmt ifs, Variable dv |
    lhsF.getTarget() = arrFld and
    (a.getLValue() = lhsF or a.getLValue().(ArrayExpr).getArrayBase() = lhsF) and
    val.getTarget()
        .getName()
        .toLowerCase()
        .matches(["%\\_ok", "%size\\_ok", "%\\_check%", "%\\_valid%", "%may\\_pull%"]) and
    ifs.getCondition().getAChild*() = val and
    a.getEnclosingStmt().getParentStmt*() = ifs.getThen() and
    // tie the validator to the stored object: both mention the same value
    val.getAnArgument().(VariableAccess).getTarget() = dv and
    a.getRValue().getAChild*().(VariableAccess).getTarget() = dv and
    // the validator must take a LENGTH/SIZE argument -- this is a byte-budget
    // validator, not a structural sanity check.  Excludes CONFIG_DEBUG_LIST
    // primitives (__list_add_valid etc.) whose args are all pointers.
    exists(Expr lenArg |
      lenArg = val.getAnArgument() and
      lenArg.getUnspecifiedType() instanceof IntegralType and
      not lenArg.getUnspecifiedType() instanceof PointerType
    )
  )
}

/** `e` reads an element of a validated-storage array field (`elems->ttlm[i]`
 *  or `elems->ttlm`). */
predicate elementFromValidatedStorage(Expr e) {
  exists(Field arrFld | validatedStorageField(arrFld) |
    e.(ArrayExpr).getArrayBase().(FieldAccess).getTarget() = arrFld or
    e.(FieldAccess).getTarget() = arrFld
  )
}

/** A pointer parameter `p` of `f` validated at the non-local source: every
 *  call site of `f` passes a validated-storage element for `p`. */
predicate validatedParam(Function f, Parameter p) {
  p.getFunction() = f and
  p.getUnspecifiedType() instanceof PointerType and
  exists(FunctionCall fc | fc.getTarget() = f) and
  forall(FunctionCall fc | fc.getTarget() = f |
    elementFromValidatedStorage(fc.getArgument(p.getIndex()))
  )
}

/** A local in `f` derived from a validated param via a field read / pointer
 *  arithmetic: `pos = ttlm->optional;`. */
predicate derivedFromValidatedParam(Function f, Variable pos) {
  exists(Parameter vp, Assignment a |
    validatedParam(f, vp) and
    a.getLValue().(VariableAccess).getTarget() = pos and
    a.getRValue().getAChild*().(VariableAccess).getTarget() = vp
  )
}

/** An argument validated at the non-local source: a validated-storage
 *  element directly, a local derived from a validated param, or a field
 *  access off a validated param. */
predicate validatedPointerArg(FunctionCall fc, Expr arg) {
  elementFromValidatedStorage(arg)
  or
  exists(Variable pos |
    derivedFromValidatedParam(fc.getEnclosingFunction(), pos) and
    arg.(VariableAccess).getTarget() = pos
  )
  or
  exists(Parameter vp |
    validatedParam(fc.getEnclosingFunction(), vp) and
    arg.getAChild*().(VariableAccess).getTarget() = vp
  )
}

/** A non-skb data-pointer parameter (the parsed cursor). */
predicate dataPointerParam(Function f, Parameter dp) {
  dp.getFunction() = f and
  dp.getUnspecifiedType() instanceof PointerType and
  not dp.getType()
        .getUnspecifiedType()
        .(PointerType)
        .getBaseType()
        .getUnspecifiedType()
        .(Struct)
        .getName() = "sk_buff"
}

/** Number of call sites where the parsed pointer is validated at the
 *  non-local source. */
int numValidatedSource(Function target, Parameter dp) {
  result =
    count(FunctionCall fc |
      fc.getTarget() = target and
      validatedPointerArg(fc, fc.getArgument(dp.getIndex()))
    )
}

from Function target, string kind, string detail
where
  target.hasDefinition() and
  (
    exists(Parameter bp, int callers, int guarded |
      boundParam(target, bp) and
      callers = numCallers(target) and
      callers > 0 and
      guarded = numGuarded(target, bp) and
      kind = "len-param" and
      detail =
        "bound=" + bp.getName() + "|callers=" + callers + "|guarded=" +
          guarded + "|verdict=" + verdict(callers, guarded)
    )
    or
    exists(int callers, int guarded |
      cursorFunction(target) and
      not exists(Parameter bp | boundParam(target, bp)) and
      callers = numCallers(target) and
      callers > 0 and
      guarded = numGuardedCursor(target) and
      kind = "cursor" and
      detail =
        "bound=p,end|callers=" + callers + "|guarded=" + guarded +
          "|verdict=" + verdict(callers, guarded)
    )
    or
    exists(Field fld |
      consumesIndexField(target, fld) and
      fieldFromRaw(fld) and
      kind = "struct-field" and
      detail = "bound=" + fld.getName() + "|verdict=" + producerVerdict(fld)
    )
    or
    exists(Parameter skb, int callers, int guarded |
      skbParam(target, skb) and
      readsSkbData(target) and
      not boundParam(target, _) and
      callers = numCallers(target) and
      callers > 0 and
      guarded = numGuardedSkb(target, skb) and
      kind = "skb-pull" and
      detail =
        "bound=skb|callers=" + callers + "|guarded=" + guarded + "|verdict=" +
          skbVerdict(target, skb)
    )
    or
    exists(Parameter dp, int callers, int guarded |
      dataPointerParam(target, dp) and
      callers = numCallers(target) and
      callers > 0 and
      guarded = numValidatedSource(target, dp) and
      // only emit when this non-local shape actually certifies a source --
      // dataPointerParam alone is far too broad
      guarded > 0 and
      kind = "validated-storage" and
      detail =
        "bound=" + dp.getName() + "|callers=" + callers + "|guarded=" + guarded +
          "|verdict=" + verdict(callers, guarded)
    )
  )
select target, target.getName() + "|kind=" + kind + "|" + detail
