/**
 * @name Unvalidated count/index into a fixed-size array (write or read OOB)
 * @description Gap #1 oracle.  Finds the dominant recent-CVE shape
 *   (~48 of the post-survey 2026 batch): an attacker-influenced scalar
 *   COUNT or INDEX -- typically a struct field read from firmware / a wire
 *   message (`num_ifs`, `num_aces`, `port_count`, `number_of_*`, `nattr`,
 *   a `*_index`/`*_id`) -- used either as a loop bound that drives writes
 *   into a FIXED-SIZE array, or directly as a subscript into one, with NO
 *   dominating guard comparing it against a compile-time bound
 *   (ARRAY_SIZE / sizeof / a MAX enum/literal).  Canonical:
 *   CVE-2026-43205 (dpaa2 num_ifs -> cfg->if_id[DPSW_MAX_IF]),
 *   CVE-2026-46197 (amdkfd nattr), CVE-2026-43113 (wl1251 packet id),
 *   CVE-2026-46122 (b43 key index).  Stage-2 (CBMC bounds-check) decides
 *   reachability with a concrete witness.
 * @kind problem
 * @id cpp/abc/unvalidated-count-index
 * @problem.severity warning
 */
import cpp
import Scope
import KernelTaintFlow

/** A name string suggesting an attacker-supplied count or index. */
bindingset[n]
predicate suspiciousName(string n) {
  n.toLowerCase()
      .matches(["num\\_%", "n\\_%", "%\\_count", "%count", "%\\_num",
                 "%\\_index", "%\\_idx", "idx", "%\\_id", "nattr",
                 "number\\_of\\_%", "port\\_count", "%\\_nr", "nr\\_%",
                 "%\\_pipe", "pipe"])
}

/** An access to a "suspicious" scalar: either a struct-field read with a
 *  suggestive name, or a parameter / local with one.  Integral type. */
class CountSource extends Expr {
  string sourceName;

  CountSource() {
    this.getUnspecifiedType() instanceof IntegralType and
    (
      exists(Field fld |
        this.(FieldAccess).getTarget() = fld and
        suspiciousName(fld.getName()) and
        sourceName = fld.getName())
      or
      exists(Variable v |
        this.(VariableAccess).getTarget() = v and
        not this instanceof FieldAccess and
        suspiciousName(v.getName()) and
        sourceName = v.getName())
    )
  }

  string getSourceName() { result = sourceName }
}

/** A fixed-size array type (compile-time element count). */
ArrayType fixedArray() { exists(result.getSize()) }

/** Holds if `e` (a count/index expr) is compared against a compile-time
 *  bound anywhere in function `f` -- proxy for "validated", excluded. */
predicate guardedAgainstConstant(CountSource e, Function f) {
  exists(RelationalOperation rel, Expr bound |
    rel.getEnclosingFunction() = f and
    rel.getAnOperand().(VariableAccess).getTarget() =
      e.(VariableAccess).getTarget() and
    bound = rel.getAnOperand() and
    bound != e and
    (bound instanceof Literal or bound instanceof SizeofOperator or
     bound instanceof EnumConstantAccess or
     bound.getValue() != "")
  )
  or
  // field form: any relational op with a sizeof/literal/enum bound naming
  // the same field anywhere in the function (conservative -> fewer FPs).
  exists(RelationalOperation rel, Field fld, Expr bound |
    rel.getEnclosingFunction() = f and
    e.(FieldAccess).getTarget() = fld and
    rel.getAnOperand().(FieldAccess).getTarget() = fld and
    bound = rel.getAnOperand() and
    (bound instanceof Literal or bound instanceof SizeofOperator or
     bound instanceof EnumConstantAccess)
  )
  or
  // min()/min_t()/clamp() idiom: the count var/field is (re)assigned the
  // result of a GNU statement-expression (the macro expansion) that
  // contains a compile-time bound.  Mirrors minClampedSink in
  // tainted_into_fixed_dest.ql.  e.g.
  //   ndev->num = min((int)ndev->num, NCI_MAX_...);
  minClamped(e, f)
}

/** The count var/field is assigned from a min/min_t/clamp StmtExpr that
 *  carries a compile-time bound. */
predicate minClamped(CountSource e, Function f) {
  exists(Assignment a, StmtExpr se, Expr bound |
    a.getEnclosingFunction() = f and
    a.getRValue() = se and
    (
      a.getLValue().(VariableAccess).getTarget() =
        e.(VariableAccess).getTarget()
      or
      a.getLValue().(FieldAccess).getTarget() = e.(FieldAccess).getTarget()
    ) and
    (bound instanceof SizeofOperator or bound instanceof Literal or
     bound instanceof EnumConstantAccess) and
    bound.getEnclosingStmt().getParentStmt*() = se.getStmt()
  )
}

/* ---- Pattern A: count drives a loop that writes a fixed array ---- */
predicate countLoopWrite(
  Function f, Loop loop, CountSource cnt, ArrayExpr write, string arrName,
  int arrSize
) {
  loop.getEnclosingFunction() = f and
  // loop bound is the suspicious count: `i < cnt`
  loop.getControllingExpr().(RelationalOperation).getAnOperand() = cnt and
  // cnt must be the loop-INVARIANT bound, not the iteration counter:
  // exclude it if it is incremented or assigned inside the loop body.
  not exists(CrementOperation cr |
    cr.getEnclosingStmt().getParentStmt*() = loop.getStmt() and
    cr.getOperand().(VariableAccess).getTarget() = countTarget(cnt)) and
  not exists(Assignment a |
    a.getEnclosingStmt().getParentStmt*() = loop.getStmt() and
    a.getLValue().(VariableAccess).getTarget() = countTarget(cnt)) and
  // body writes into a fixed-size array, indexed by something
  write.getEnclosingStmt().getParentStmt*() = loop.getStmt() and
  write.getArrayBase().getType().getUnspecifiedType() = fixedArray() and
  arrSize = write.getArrayBase().getType().getUnspecifiedType().(ArrayType).getArraySize() and
  arrName = write.getArrayBase().toString() and
  // the write is an lvalue (assigned into)
  exists(Assignment a | a.getLValue() = write) and
  not guardedAgainstConstant(cnt, f) and
  not maskGuarded(cnt, f) and
  not bareParameter(cnt, f)
}

/** Holds if `e` is a bare incoming PARAMETER value used directly (the
 *  parameter is never assigned within `f`).  Such an index/count is the
 *  CALLER's responsibility to bound, so flagging it at function
 *  granularity is a recall-only false positive (e.g. mac80211
 *  `link[link_id]` where `link_id` is a validated parameter).  We keep
 *  indices that are DECODED in-function (a local assigned from untrusted
 *  data, e.g. b43 `keyidx` from the RX descriptor) or struct FIELDS
 *  (wire fields, e.g. nci `n_targets`). */
predicate bareParameter(CountSource e, Function f) {
  exists(Parameter p |
    e.(VariableAccess).getTarget() = p and
    p.getFunction() = f and
    not exists(Assignment a |
      a.getEnclosingFunction() = f and
      a.getLValue().(VariableAccess).getTarget() = p) and
    not exists(CrementOperation cr |
      cr.getEnclosingFunction() = f and
      cr.getOperand().(VariableAccess).getTarget() = p))
}

/** The variable/field underlying a count source (for crement/assign checks). */
Declaration countTarget(CountSource c) {
  result = c.(VariableAccess).getTarget() or
  result = c.(FieldAccess).getTarget()
}

/** Confidence tier for a count/index hit.  HIGH when the value is
 *  raw-taint-reachable (flows from a modelled untrusted wire/netlink/user
 *  source, or is a field of a struct bulk-filled from raw bytes -- cgw
 *  `result_idx`, ceph, b43 `key_index`); MEDIUM otherwise (suspicious name
 *  + unguarded, but no taint evidence -- e.g. dpaa2 `num_ifs`, the
 *  mac80211 `link_id` cluster, which taint cannot separate).  See
 *  KernelTaintFlow.qll. */
string confidence(CountSource e) {
  if KernelTaintFlow::isRawTaintReachable(e) then result = "HIGH" else result = "MEDIUM"
}

/** Holds if the index/count `e` is masked with a compile-time constant in
 *  `f` -- `idx &= CONST` or `idx & CONST` -- which bounds it as soundly as
 *  a relational guard (e.g. `can_id &= CAN_SFF_MASK` before `rx_sff[id]`).
 *  Masking uses bitwise-and, not a RelationalOperation, so the relational
 *  guard predicate misses it. */
predicate maskGuarded(CountSource e, Function f) {
  exists(AssignAndExpr aa, Expr k |
    aa.getEnclosingFunction() = f and
    aa.getLValue().(VariableAccess).getTarget() = countTarget(e) and
    k = aa.getRValue() and
    (k instanceof Literal or k instanceof EnumConstantAccess or
     k.getValue() != ""))
  or
  exists(BitwiseAndExpr ba, Expr k |
    ba.getEnclosingFunction() = f and
    ba.getAnOperand().(VariableAccess).getTarget() = countTarget(e) and
    k = ba.getAnOperand() and
    (k instanceof Literal or k instanceof EnumConstantAccess or
     k.getValue() != ""))
}

/* ---- Pattern B: index subscripts a fixed array directly ---- */
predicate directIndex(
  Function f, ArrayExpr ae, CountSource idx, string arrName, int arrSize
) {
  ae.getEnclosingFunction() = f and
  ae.getArrayOffset() = idx and
  ae.getArrayBase().getType().getUnspecifiedType() = fixedArray() and
  arrSize = ae.getArrayBase().getType().getUnspecifiedType().(ArrayType).getArraySize() and
  arrName = ae.getArrayBase().toString() and
  // enum-typed indices index arrays sized by that enum -> bounded idiom.
  not idx.getUnspecifiedType() instanceof Enum and
  not guardedAgainstConstant(idx, f) and
  not maskGuarded(idx, f) and
  not bareParameter(idx, f)
}

/** Impact tier of a count/index hit.  WRITE (high impact -- OOB write,
 *  the primitive that escalates to control-flow / data corruption) when
 *  the array access is an assignment destination; READ (lower impact --
 *  OOB read / info-leak / DoS) otherwise.  The talk's point: triage must
 *  separate write/control primitives from read/DoS, and LLMs overstate the
 *  latter as the former. */
predicate writeImpact(ArrayExpr ae) { exists(Assignment a | a.getLValue() = ae) }

/** Impact string for a direct-index access. */
string indexImpact(ArrayExpr ae) {
  if writeImpact(ae) then result = "WRITE" else result = "READ"
}

from Function f, string kind, string name, int line, string detail
where
  inScope(f) and
  name = f.getName() and
  (
    exists(Loop loop, CountSource cnt, ArrayExpr w, string an, int sz |
      countLoopWrite(f, loop, cnt, w, an, sz) and
      kind = "count-loop-write" and
      line = loop.getLocation().getStartLine() and
      detail =
        "count=" + cnt.getSourceName() + "|arr=" + an + "[" + sz + "]" +
          "|confidence=" + confidence(cnt) + "|impact=WRITE"
    )
    or
    exists(ArrayExpr ae, CountSource idx, string an, int sz |
      directIndex(f, ae, idx, an, sz) and
      kind = "direct-index" and
      line = ae.getLocation().getStartLine() and
      detail =
        "index=" + idx.getSourceName() + "|arr=" + an + "[" + sz + "]" +
          "|confidence=" + confidence(idx) + "|impact=" + indexImpact(ae)
    )
  )
select f,
  name + "|" + f.getFile().getAbsolutePath() + "|" + line.toString() + "|" +
  kind + "|" + detail
