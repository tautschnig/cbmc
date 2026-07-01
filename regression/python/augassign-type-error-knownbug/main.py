# KNOWNBUG: augmented assignment applies the binary operator, so `x += "a"` for an
# int x is `int + str` -> TypeError. The AugAssign handler does not apply the
# binary-op type check that the plain `+` path does. Desired: VERIFICATION FAILED.
x = 1
x += "a"
