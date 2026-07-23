# KNOWNBUG (false PROOF residual) -- the missing-return_* cluster root.
# A function that falls through (no return on some path) into an
# int-annotated slot returns None, emitted as the int SENTINEL. When that
# value flows on as a call ARGUMENT into an int-typed PARAMETER, the
# sentinel (a huge negative int) is compared numerically -- no TypeError.
# absolute(5) falls through -> None; absolute(None) then does `x < 0` on
# the sentinel -> huge negative -> returns -x. DESIRED: VERIFICATION FAILED
# (CPython raises "TypeError: '<' not supported between 'NoneType' and
# 'int'" at the inner comparison). CURRENT: VERIFICATION SUCCESSFUL.
# Needs None-preservation across the argument->int-slot coercion (the
# None-through-typed-slot representation pass; architecture inventory A,
# plan section 0). The pv-operand and None-symbol ordering faces are
# already closed. When fixed -> promote to CORE.
def absolute(x: int) -> int:
    if x < 0:
        return -x
    # fall through on the non-negative path -> returns None


a = absolute(absolute(5))
assert a == a
