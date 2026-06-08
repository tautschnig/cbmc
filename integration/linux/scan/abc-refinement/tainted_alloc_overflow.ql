/**
 * @name User value reaches an arithmetic allocation size (overflow risk)
 * @description Flags an allocation whose size argument is an arithmetic
 *   expression (`*` or `+`) into which a user-controlled length flows,
 *   using a raw allocator (kmalloc/kzalloc/vmalloc/kvmalloc/...) rather
 *   than the overflow-checked helpers (kmalloc_array/kcalloc/struct_size/
 *   array_size).  The multiplication/addition can wrap, yielding an
 *   undersized buffer that a later copy overflows.  CBMC (stage 2)
 *   decides the arithmetic exactly (--unsigned-overflow-check).
 * @kind problem
 * @id cpp/abc/tainted-alloc-overflow
 * @problem.severity warning
 */
import cpp
import semmle.code.cpp.dataflow.new.TaintTracking
import semmle.code.cpp.dataflow.new.DataFlow

/** A raw allocator (NOT the overflow-checked *_array / *calloc forms).
 *  Names are the linux-6.12 lowered forms (alloc-profiling `_noprof`)
 *  plus common driver wrappers. */
class RawAllocCall extends FunctionCall {
  RawAllocCall() {
    this.getTarget().getName() =
      ["kmalloc_noprof", "kzalloc_noprof", "vmalloc_noprof", "vzalloc_noprof",
       "__kvmalloc_node_noprof", "kmalloc_node_noprof", "kzalloc_node_noprof",
       "_rtw_malloc", "_rtw_zmalloc", "rtw_malloc", "rtw_zmalloc",
       "devm_kmalloc", "devm_kzalloc"]
  }

  /** The size argument: index 1 for the devm_* forms (dev, size, gfp),
   *  index 0 otherwise. */
  Expr getSizeArg() {
    if this.getTarget().getName().matches("devm_%")
    then result = this.getArgument(1)
    else result = this.getArgument(0)
  }
}

/** The size argument contains a multiplication or addition. */
predicate arithmeticSize(RawAllocCall a) {
  exists(Expr e |
    e = a.getSizeArg().getAChild*() and
    (e instanceof MulExpr or e instanceof AddExpr))
}

module CfgImpl implements DataFlow::ConfigSig {
  predicate isSource(DataFlow::Node s) {
    exists(Parameter p |
      p.getName() = ["count", "len", "size", "n", "nbytes", "length", "num"] and
      p.getUnderlyingType() instanceof IntegralType and
      s.asExpr() = p.getAnAccess())
  }

  predicate isSink(DataFlow::Node s) {
    exists(RawAllocCall a | arithmeticSize(a) and
      s.asExpr() = a.getSizeArg().getAChild*())
  }
}

module Flow = TaintTracking::Global<CfgImpl>;

from DataFlow::Node source, DataFlow::Node sink, RawAllocCall a, Parameter p
where
  Flow::flow(source, sink) and
  arithmeticSize(a) and
  sink.asExpr() = a.getSizeArg().getAChild*() and
  source.asExpr() = p.getAnAccess()
select a,
  a.getEnclosingFunction().getName() + "|" +
  a.getFile().getAbsolutePath() + "|" +
  a.getLocation().getStartLine().toString() + "|" +
  a.getTarget().getName() + "|" + p.getName() + "|" +
  a.getSizeArg().toString()
