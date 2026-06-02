# Companion to unbound-local-cross-branch: when x is assigned on BOTH
# branches it is always bound, so no UnboundLocalError must be reported
# (the is-bound flag is true on every path that reaches the read).
def f(c: bool) -> int:
    if c:
        x = 1
    else:
        x = 2
    return x


assert f(True) == 1
assert f(False) == 2
