/**
 * @name A5 authorization (netlink): genl handler not GENL_ADMIN_PERM-gated
 * @description Net-specific refinement of the A5 authorization template.
 *   Generic-netlink handlers gate authorization at REGISTRATION -- the
 *   `genl_ops`/`genl_small_ops` entry's `.flags` field carries
 *   `GENL_ADMIN_PERM` (or `GENL_UNS_ADMIN_PERM`), which the netlink core
 *   enforces before the `.doit`/`.dumpit` handler runs -- rather than via
 *   an inline `capable()` check.  So `auth_missing_capable.ql` would
 *   wrongly flag an admin-perm-gated handler.  This finder classifies each
 *   registered handler: ADMIN-PERM (registration-guarded -> mitigated for
 *   A5) vs NO-ADMIN-PERM (must self-check, or is intentionally
 *   unprivileged).  A NO-ADMIN-PERM handler doing state-changing work with
 *   no inline capable() is the genuine A5 concern.
 * @kind problem
 * @id cpp/abc/a5-genl-admin-perm
 * @problem.severity warning
 */
import cpp

/** A generic-netlink ops table entry type. */
predicate genlOpsType(Type t) {
  t.getName() = ["genl_ops", "genl_small_ops", "genl_split_ops"]
}

/** The flags initializer of `lit` mentions an *_ADMIN_PERM capability.
 *  GENL_ADMIN_PERM / GENL_UNS_ADMIN_PERM are MACROS (numeric), not enum
 *  constants, so match the macro invocation within the flags initializer. */
predicate adminPermGated(ClassAggregateLiteral lit) {
  exists(Field flags, MacroInvocation mi |
    flags.getName() = "flags" and
    mi.getMacroName().toUpperCase().matches("%ADMIN_PERM%") and
    mi.getExpr() = lit.getAFieldExpr(flags).getAChild*()
  )
}

string verdict(ClassAggregateLiteral lit) {
  if adminPermGated(lit)
  then result = "ADMIN-PERM (registration-guarded)"
  else result = "NO-ADMIN-PERM (needs inline authz)"
}

from ClassAggregateLiteral lit, Field op, Function handler
where
  genlOpsType(lit.getType().getUnspecifiedType()) and
  op.getName() = ["doit", "dumpit"] and
  lit.getAFieldExpr(op).(FunctionAccess).getTarget() = handler
select handler,
  handler.getName() + "|" + handler.getFile().getAbsolutePath() + "|" +
    handler.getLocation().getStartLine().toString() + "|A5-netlink:" +
    op.getName() + "|" + verdict(lit)
