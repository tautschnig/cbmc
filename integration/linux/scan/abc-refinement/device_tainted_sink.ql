/**
 * @name Device-controlled value used as index / length / loop bound / shift
 * @description Device-as-attacker collector.  Sources: fields of
 *   DMA-writable structs and MMIO reads (DeviceTaint.qll).  Sinks: array
 *   subscripts, memcpy-family lengths, loop bounds, shift amounts.  A
 *   field assigned from device data is tainted wherever it is read
 *   (field-level association, as in KernelTaintFlow).  Advisories rank;
 *   they never drop results.
 * @kind problem
 * @id cpp/abc/device-tainted-sink
 * @problem.severity warning
 */

import cpp
import Scope
import DeviceTaint
import semmle.code.cpp.dataflow.new.TaintTracking
import semmle.code.cpp.dataflow.new.DataFlow

/** A sink expression and its kind. */
predicate sinkExpr(Expr e, string kind) {
  DeviceTaint::inDriver(e) and
  (
    exists(ArrayExpr ae | e = ae.getArrayOffset() |
      if exists(ae.getArrayBase().getUnspecifiedType().(ArrayType).getArraySize())
      then kind = "index-fixed"
      else kind = "index-ptr"
    )
    or
    exists(FunctionCall fc, int k |
      e = fc.getArgument(k) and
      (
        fc.getTarget().getName() =
          [
            "memcpy", "__builtin_memcpy", "__memcpy", "memmove",
            "__builtin_memmove", "memcpy_fromio", "memcpy_toio",
            "__underlying_memcpy", "__underlying_memmove"
          ] and
        k = 2
        or
        fc.getTarget().getName() = ["memset", "__builtin_memset", "__underlying_memset"] and
        k = 2
        or
        fc.getTarget().getName() =
          ["copy_to_user", "_copy_to_user", "copy_from_user", "_copy_from_user"] and
        k = 2
      ) and
      kind = "copy-len"
    )
    or
    exists(Loop l, RelationalOperation rel |
      rel = l.getControllingExpr() and
      e = rel.getAnOperand() and
      // the loop-invariant bound, not the iteration variable
      not exists(Expr upd |
        upd.getEnclosingStmt().getParentStmt*() = l and
        (
          upd.(CrementOperation).getOperand().(VariableAccess).getTarget() =
            e.(VariableAccess).getTarget()
          or
          upd.(Assignment).getLValue().(VariableAccess).getTarget() =
            e.(VariableAccess).getTarget()
        )
      ) and
      kind = "loop-bound"
    )
    or
    exists(LShiftExpr sh | e = sh.getRightOperand() and kind = "shift")
  )
}

module DevCfg implements DataFlow::ConfigSig {
  predicate isSource(DataFlow::Node n) { DeviceTaint::isDeviceSource(n.asExpr()) }

  predicate isSink(DataFlow::Node n) { sinkExpr(n.asExpr(), _) }

  /** store to field f  ~>  any driver read of f */
  predicate isAdditionalFlowStep(DataFlow::Node a, DataFlow::Node b) {
    exists(Assignment asg, Field f, FieldAccess rd |
      DeviceTaint::inDriver(asg) and
      asg.getLValue().(FieldAccess).getTarget() = f and
      a.asExpr() = asg.getRValue() and
      rd.getTarget() = f and
      DeviceTaint::inDriver(rd) and
      not rd = any(Assignment x).getLValue() and
      b.asExpr() = rd
    )
  }
}

module DevFlow = TaintTracking::Global<DevCfg>;

/** Variables/fields read inside `e`. */
Variable keyOf(Expr e) { result = e.getAChild*().(VariableAccess).getTarget() }

/** A relational comparison on the same variable/field in the sink's
 *  function, other than a loop-bound sink's own controlling expression. */
predicate locallyGuarded(Expr sink) {
  exists(RelationalOperation rel |
    rel.getEnclosingFunction() = sink.getEnclosingFunction() and
    not rel.getAnOperand() = sink and
    keyOf(rel.getAnOperand()) = keyOf(sink)
  )
  or
  // min()/clamp() statement-expression assigned to the same key
  exists(Assignment a |
    a.getEnclosingFunction() = sink.getEnclosingFunction() and
    a.getRValue() instanceof StmtExpr and
    a.getLValue().(VariableAccess).getTarget() = keyOf(sink)
  )
}

/** The sink expression is masked by a constant: `x & MASK`, `x % N`. */
predicate masked(Expr sink) {
  exists(BinaryBitwiseOperation b | b = sink.getAChild*() and b instanceof BitwiseAndExpr and
    exists(b.getAnOperand().getValue()))
  or
  exists(RemExpr r | r = sink.getAChild*())
}

/** The sink's own type caps it below a fixed array's size. */
predicate typeBounded(Expr sink) {
  exists(ArrayExpr ae, int sz, IntegralType t |
    sink = ae.getArrayOffset() and
    sz = ae.getArrayBase().getUnspecifiedType().(ArrayType).getArraySize() and
    t = sink.getUnspecifiedType() and
    t.isUnsigned() and
    t.getSize() < 4 and
    sz >= 1.bitShiftLeft(t.getSize() * 8)
  )
}

string adv(boolean b) { b = true and result = "yes" or b = false and result = "no" }

from DevFlow::PathNode src, DevFlow::PathNode snk, Expr s, string kind, Function f
where
  DevFlow::flowPath(src, snk) and
  s = snk.getNode().asExpr() and
  sinkExpr(s, kind) and
  f = s.getEnclosingFunction() and
  inScope(f)
select f,
  f.getName() + "|" + s.getFile().getAbsolutePath() + "|" + s.getLocation().getStartLine() + "|" +
    kind + "|sink=" + s.toString() + "|src=" + DeviceTaint::sourceName(src.getNode().asExpr()) +
    "|srcloc=" + src.getNode().getLocation().getFile().getBaseName() + ":" +
    src.getNode().getLocation().getStartLine() + "|adv_guard=" +
    adv(any(boolean b | if locallyGuarded(s) then b = true else b = false)) + "|adv_mask=" +
    adv(any(boolean b | if masked(s) then b = true else b = false)) + "|adv_typebound=" +
    adv(any(boolean b | if typeBounded(s) then b = true else b = false))
