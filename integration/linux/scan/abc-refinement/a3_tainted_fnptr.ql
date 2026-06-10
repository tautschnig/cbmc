/**
 * @name A3 integrity/CFI: non-function value written into a function pointer
 * @description THREAT TEMPLATE for the integrity / control-flow-integrity
 *   asset (A3): an assignment that stores a COMPUTED / CAST value (not the
 *   address of a named function, not NULL, not a copy of another function
 *   pointer) into a function-pointer-typed field or variable.
 *     asset   = control-flow integrity (indirect-call target is legitimate)
 *     actor   = attacker who can influence the stored value
 *     threat  = the function pointer is set outside the legitimate target
 *               set -> control-flow hijack at the later indirect call
 *     obligation (CBMC) = the stored pointer is in {legit targets} (a3_test.c)
 *   Direct writes of a non-function value into a function pointer are rare
 *   in well-formed code (legitimate ones use a named function / NULL / an
 *   ops-table copy), so a hit is a strong A3 signal.  The complementary
 *   INDIRECT A3 threat -- a UAF/OOB write landing on an ops field -- is
 *   found by the memory-safety family (A1); this template covers the direct
 *   write and the A3 obligation assesses the impact of both.
 * @kind problem
 * @id cpp/abc/cfi-tainted-fnptr-write
 * @problem.severity warning
 */
import cpp

/** A function-pointer-typed target (field or variable). */
predicate fnPtrTarget(Variable v) {
  v.getType().getUnspecifiedType() instanceof FunctionPointerType
}

from Assignment a, Variable v, Expr rhs
where
  rhs = a.getRValue() and
  (
    a.getLValue().(VariableAccess).getTarget() = v or
    a.getLValue().(FieldAccess).getTarget() = v
  ) and
  fnPtrTarget(v) and
  // exclude legitimate static assignments
  not rhs instanceof FunctionAccess and // = a named function
  not rhs.getValue() = "0" and // = NULL
  not rhs instanceof AddressOfExpr and // = &func
  // suspicious: a cast of a non-function value (e.g. (op_t)attacker_long)
  // or a plain non-function-pointer value coerced in
  (
    rhs.(Cast).getExpr().getType().getUnspecifiedType() instanceof IntegralType
    or
    rhs.getType().getUnspecifiedType() instanceof IntegralType
  )
select a,
  a.getEnclosingFunction().getName() + "|" +
    a.getLocation().getFile().getAbsolutePath() + "|" +
    a.getLocation().getStartLine().toString() +
    "|A3 CFI: non-function value written into function pointer '" +
    v.getName() + "'"
