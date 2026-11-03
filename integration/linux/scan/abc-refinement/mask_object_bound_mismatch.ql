/**
 * @name Ring-masked index into a caller-supplied array (object/bound mismatch)
 * @description Prototype for the gap the Velocity Labs reports exposed.  An
 *   index is bounded by a *ring* quantity (`& size_mask`, `% ring_size`, a
 *   modulus stored in a struct field) and then used to subscript an object
 *   whose length is a *different* quantity in scope -- a caller-supplied
 *   pointer parameter (`tx_pkts`, length `nb_pkts`) or a heap array whose
 *   length is a sibling field (`dev->eqs`, length `dev->neqs`).  Masking is a
 *   mitigation only when the modulus matches the size of the object being
 *   indexed; when it does not, the mask IS the bug.
 *
 *   Needs no attacker-controlled value, which is why the taint-driven
 *   collectors cannot express it: amzn-drivers
 *   `eth_ena_xmit_pkts` prefetches `tx_pkts[(sent_idx + 4) & size_mask]`
 *   past the caller's `nb_pkts` entries.
 * @kind problem
 * @id cpp/abc/mask-object-bound-mismatch
 * @problem.severity warning
 */

import cpp

/** A modulus held in a struct field or local whose name denotes a ring size. */
predicate ringModulus(Expr m, string name) {
  exists(Field f |
    m.(FieldAccess).getTarget() = f and
    name = f.getName() and
    name.toLowerCase().matches(["%mask%", "%ring_size%", "%size_mask%", "%q_depth%",
                                 "%queue_mask%", "%ring_mask%"])
  )
  or
  exists(Variable v |
    m.(VariableAccess).getTarget() = v and
    not m instanceof FieldAccess and
    name = v.getName() and
    name.toLowerCase().matches(["%mask%", "%ring_size%", "%queue_mask%"])
  )
}

/** `e` is `... & <ring modulus>` (possibly through the ENA/ring macros). */
predicate maskedByRing(Expr e, string modname) {
  exists(BitwiseAndExpr a | a = e.getAChild*() | ringModulus(a.getAnOperand(), modname))
  or
  exists(RemExpr r | r = e.getAChild*() | ringModulus(r.getAnOperand(), modname))
}

/**
 * The subscripted object and the name of the quantity that actually bounds
 * it: a pointer parameter (bounded by another parameter of the function) or
 * a heap array field (bounded by a sibling count field).
 */
predicate boundedObject(ArrayExpr ae, string obj, string boundName) {
  // caller-supplied array parameter: tx_pkts[...] with nb_pkts in scope
  exists(Parameter p, Parameter n |
    ae.getArrayBase().(VariableAccess).getTarget() = p and
    p.getUnspecifiedType() instanceof PointerType and
    n.getFunction() = p.getFunction() and
    n != p and
    n.getUnspecifiedType() instanceof IntegralType and
    n.getName().toLowerCase().matches(["%nb_%", "%num%", "%count%", "%cnt%", "%n_%",
                                        "nb", "n"]) and
    obj = p.getName() and
    boundName = n.getName()
  )
  or
  // heap array field indexed with a sibling count field: dev->eqs / dev->neqs
  exists(Field arr, Field cnt |
    ae.getArrayBase().(FieldAccess).getTarget() = arr and
    arr.getUnspecifiedType() instanceof PointerType and
    cnt.getDeclaringType() = arr.getDeclaringType() and
    cnt.getUnspecifiedType() instanceof IntegralType and
    // the count must be named after the array (neqs/eqs, num_foo/foo),
    // not just any count-ish field of the same struct
    cnt.getName().toLowerCase() =
      ["n" + arr.getName().toLowerCase(), arr.getName().toLowerCase() + "s",
       "num_" + arr.getName().toLowerCase(), arr.getName().toLowerCase() + "_count",
       arr.getName().toLowerCase() + "_num"] and
    obj = arr.getName() and
    boundName = cnt.getName()
  )
}

/*
 * No "is it guarded?" advisory here, deliberately.  The loop that contains
 * the access is normally bounded by the right quantity already
 * (`sent_idx < nb_pkts`), so asking whether the bound appears in some
 * comparison in the function suppresses exactly the bug.  The signal is the
 * mask on the subscript: the correct form of this code indexes the object
 * directly under a guard (`if ((i + 4) < count) ... mbufs[i + 4]`, the RX
 * refill loop), which has no mask and so is not reported.
 */
from ArrayExpr ae, Function f, string obj, string boundName, string modname
where
  f = ae.getEnclosingFunction() and
  boundedObject(ae, obj, boundName) and
  maskedByRing(ae.getArrayOffset(), modname) and
  // If the modulus is named after the object (qp_table indexed with
  // qp_table_mask), it IS that object's size and the mask is correct.  The
  // finding is a modulus belonging to some *other* object (a ring).
  not modname.toLowerCase().matches("%" + obj.toLowerCase() + "%")
select f,
  f.getName() + "|" + ae.getFile().getAbsolutePath() + "|" +
    ae.getLocation().getStartLine() + "|mask-object-mismatch|obj=" + obj +
    "|masked_by=" + modname + "|actual_bound=" + boundName
