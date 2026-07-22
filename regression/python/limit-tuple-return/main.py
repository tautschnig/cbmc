# PLR §7.6: unpacking a COMPUTED tuple return `x, y = swap(1, 2)` where
# `swap` returns `b, a` (parameter-dependent, not a constant literal).
# The `-> tuple` return-type annotation erases the result to a
# python_value; the unpack takes the SOUND nondet floor (a constant
# tuple return like `pair() -> (100, 200)` is still precise via
# function_returned_literal, but a value-dependent return needs
# call-return value propagation -- the per-instance-provenance family).
# Desired: VERIFICATION SUCCESSFUL (x==2, y==1). KNOWNBUG until the
# pv-tuple return unwrap propagates element values.
def swap(a: int, b: int) -> tuple:
    return b, a


x, y = swap(1, 2)
assert x == 2
assert y == 1
