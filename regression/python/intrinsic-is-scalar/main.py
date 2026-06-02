# CBMC models `is`/`is not` on non-None int/float/str operands as
# value (in)equality, not object identity (differential2 §3/A): it has
# no small-int cache / interning model. So `a is b` becomes a == b. For
# 256 == 256 that coincidentally matches CPython (small ints are
# cached), and verification succeeds -- but a warning is emitted to
# flag that object identity is not actually modeled.
a = 256
b = 256
assert a is b
