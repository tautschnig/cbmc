# Differential unsoundness witness §5 (cbmc-py-differential):
# a `return` (or break/continue) inside `finally` must replace the
# pending return. CPython: f() == 2. cbmc-py appends the finally body
# after the handler chain but does not reroute return/break/continue,
# so the try's `return 1` fires and f() models as 1 -> the assert is a
# false alarm. KNOWNBUG until convert_try reroutes finally control flow.
def f() -> int:
    try:
        return 1
    finally:
        return 2
assert f() == 2
