/**
 * Device-as-attacker taint sources.
 *
 * Models data the *device* controls: memory it can DMA into (coherent
 * rings, feature/response buffers, completion descriptors) and MMIO
 * register reads.  KernelTaint/KernelTaintFlow cover user/wire/firmware
 * input but not this surface, which matters when the device (or the
 * host emulating it) is outside the guest's trust boundary, e.g.
 * confidential-computing guests, or plain hardening against firmware bugs.
 *
 * Over-approximate by design (collector stage): a struct type is
 * "device-written" if it is reachable from a DMA-coherent allocation via
 * pointer provenance, struct-pointer casts, or nesting.  Reads of any
 * field of such a type are sources.  Ranking happens in the query's
 * advisories, not here.
 */

import cpp

module DeviceTaint {
  /**
   * Driver code, as opposed to generic kernel code.  Two DB shapes have to
   * be handled: an out-of-tree driver (source root IS the driver, so every
   * relative path qualifies) and an in-tree subsystem DB (where the kernel's
   * own `include/` headers are present and must be excluded, or every
   * inlined `skbuff.h` / `bpf.h` helper becomes a candidate).
   */
  predicate inDriver(Element e) {
    exists(string p | p = e.getFile().getRelativePath() |
      not p.matches("include/%") and
      not p.matches("arch/%/include/%") and
      not p.matches("%/include/linux/%")
    )
  }

  /**
   * A hardware-ABI struct: declared in driver-local code rather than in a
   * generic kernel header.  Device register/descriptor/admin-response
   * layouts live next to the driver (ena_admin_defs.h, efa_io_defs.h, ...),
   * whereas `sk_buff`, `net_device` and `bpf_map` do not -- without this
   * distinction the nesting rule below drags in half the kernel.
   */
  predicate driverLocalStruct(Struct s) { inDriver(s) }

  /** A function that returns a buffer it DMA-mapped (driver wrapper). */
  predicate isDmaMappingWrapper(Function g) {
    inDriver(g) and
    exists(ReturnStmt r, LocalScopeVariable v, FunctionCall map |
      r.getEnclosingFunction() = g and
      r.getExpr().(VariableAccess).getTarget() = v and
      map.getEnclosingFunction() = g and
      map.getTarget().getName() = ["dma_map_single", "dma_map_single_attrs"] and
      map.getArgument(1).(VariableAccess).getTarget() = v
    )
  }

  /** A call returning CPU-visible memory the device can write. */
  predicate isDmaAllocCall(FunctionCall fc) {
    fc.getTarget().getName() =
      [
        "dma_alloc_coherent", "dma_zalloc_coherent", "dmam_alloc_coherent",
        "dma_alloc_attrs", "dma_alloc_noncoherent", "dma_pool_alloc",
        "dma_pool_zalloc"
      ]
    or
    isDmaMappingWrapper(fc.getTarget())
  }

  /** `e` (or a sub-expression) is a DMA allocation or reads DMA storage. */
  predicate mentionsDma(Expr e) {
    exists(Expr c | c = e.getUnconverted().getAChild*() |
      isDmaAllocCall(c)
      or
      dmaStorage(c.(VariableAccess).getTarget())
    )
  }

  /**
   * A variable or field that holds a pointer into device-writable memory.
   * Propagates through driver-local assignments and call arguments.
   */
  predicate dmaStorage(Variable v) {
    v.getUnspecifiedType() instanceof PointerType and
    (
      exists(Assignment a |
        inDriver(a) and
        a.getLValue().(VariableAccess).getTarget() = v and
        mentionsDma(a.getRValue())
      )
      or
      exists(Initializer i |
        inDriver(i) and i.getDeclaration() = v and mentionsDma(i.getExpr())
      )
      or
      // parameter receiving a DMA pointer at some driver call site
      exists(FunctionCall call, int k |
        inDriver(call) and
        inDriver(call.getTarget()) and
        v = call.getTarget().getParameter(k) and
        mentionsDma(call.getArgument(k))
      )
    )
  }

  /** The struct (or union) behind a pointer/array/plain type. */
  Struct structOf(Type t) {
    result = t.getUnspecifiedType()
    or
    result = t.getUnspecifiedType().(PointerType).getBaseType().getUnspecifiedType()
    or
    result = t.getUnspecifiedType().(ArrayType).getBaseType().getUnspecifiedType()
  }

  /** Pointee type of a cast operand that is `&x` or a pointer. */
  Type ptrOperandType(Expr e) {
    result = e.(AddressOfExpr).getOperand().getType()
    or
    not e instanceof AddressOfExpr and
    result = e.getUnspecifiedType().(PointerType).getBaseType()
  }

  /** A struct type whose instances the device can write. */
  predicate deviceStruct(Struct s) {
    // (a) pointee of DMA storage
    exists(Variable v | dmaStorage(v) and s = structOf(v.getType()) and driverLocalStruct(s))
    or
    // (b) a DMA pointer cast to a struct pointer: (T *)(ring + off)
    exists(Cast c |
      inDriver(c) and
      s = structOf(c.getType()) and
      driverLocalStruct(s) and
      c.getUnspecifiedType() instanceof PointerType and
      mentionsDma(c.getExpr())
    )
    or
    // (c) a struct pointer re-typed by a cast: (acq_entry *)&resp,
    //     (acq_entry *)get_resp -- the device type flows both ways
    exists(Cast c, Struct other, Struct opnd |
      inDriver(c) and
      c.getUnspecifiedType() instanceof PointerType and
      opnd = structOf(ptrOperandType(c.getExpr().getUnconverted())) and
      (
        other = structOf(c.getType()) and s = opnd
        or
        s = structOf(c.getType()) and other = opnd
      ) and
      deviceStruct(other)
    )
    or
    // (d) nested member types (resp.u.device_attr ...), restricted to the
    //     same declaration file so this follows the hardware ABI grouping
    //     instead of walking into generic kernel types
    exists(Struct outer, Field f |
      deviceStruct(outer) and
      f.getDeclaringType() = outer and
      s = structOf(f.getType()) and
      driverLocalStruct(s) and
      s.getFile() = outer.getFile()
    )
  }

  /** A read of a device-written field. */
  class DeviceFieldRead extends FieldAccess {
    DeviceFieldRead() {
      deviceStruct(this.getTarget().getDeclaringType()) and
      inDriver(this) and
      this.getUnspecifiedType() instanceof IntegralType and
      not this = any(Assignment a).getLValue() and
      not this = any(CrementOperation op).getOperand()
    }
  }

  /** An MMIO register read. */
  class MmioRead extends FunctionCall {
    MmioRead() {
      this.getTarget().getName() =
        [
          "readb", "readw", "readl", "readq", "ioread8", "ioread16",
          "ioread32", "ioread64", "__raw_readl", "__raw_readq",
          "readl_relaxed", "readq_relaxed", "ioread32be"
        ]
    }
  }

  /**
   * A loop whose termination depends on device data (an exit condition,
   * or an `if` guarding break/return/goto, reads a device field directly
   * or through a local), e.g. ENA's "count descriptors until LAST bit".
   */
  predicate deviceControlledLoop(Loop l) {
    exists(Expr cond, DeviceFieldRead r |
      (
        cond = l.getControllingExpr()
        or
        exists(IfStmt i |
          i.getParentStmt+() = l.getStmt() and
          cond = i.getCondition() and
          (
            i.getThen().getAChild*() instanceof BreakStmt or
            i.getThen().getAChild*() instanceof ReturnStmt or
            i.getThen().getAChild*() instanceof GotoStmt or
            i.getThen() instanceof BreakStmt or
            i.getThen() instanceof ReturnStmt
          )
        )
      ) and
      r.getEnclosingFunction() = l.getEnclosingFunction() and
      (
        r = cond.getAChild*()
        or
        exists(LocalScopeVariable v |
          v.getAnAccess() = cond.getAChild*() and
          exists(Assignment a |
            a.getLValue().(VariableAccess).getTarget() = v and
            r = a.getRValue().getAChild*()
          )
        )
      )
    )
  }

  /** A counter incremented inside a device-controlled loop. */
  predicate deviceLoopCounter(LocalScopeVariable v, Loop l) {
    inDriver(l) and
    deviceControlledLoop(l) and
    exists(CrementOperation c |
      c.getEnclosingStmt().getParentStmt*() = l.getStmt() and
      c.getOperand().(VariableAccess).getTarget() = v
    )
  }

  /** A read, after the loop, of a counter the device controls by implicit flow. */
  class DeviceLoopCount extends VariableAccess {
    Loop loop;

    DeviceLoopCount() {
      deviceLoopCounter(this.getTarget(), loop) and
      this.getEnclosingFunction() = loop.getEnclosingFunction() and
      not this.getEnclosingStmt().getParentStmt*() = loop and
      this.getLocation().getStartLine() > loop.getLocation().getEndLine() and
      not this = any(Assignment a).getLValue()
    }
  }

  /** Name of the device datum an expression reads (for reporting). */
  string sourceName(Expr e) {
    result = e.(DeviceFieldRead).getTarget().getDeclaringType().getName() + "." +
        e.(DeviceFieldRead).getTarget().getName()
    or
    result = "mmio:" + e.(MmioRead).getTarget().getName()
    or
    result = "loopcount:" + e.(DeviceLoopCount).getTarget().getName()
  }

  predicate isDeviceSource(Expr e) {
    e instanceof DeviceFieldRead or e instanceof MmioRead or e instanceof DeviceLoopCount
  }
}
