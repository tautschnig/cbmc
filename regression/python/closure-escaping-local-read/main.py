# Closure cell substrate (PLR §4.2.2), read-only escaping slice.
# A factory function returns a lambda that closes over the factory's
# LOCAL variables (not just its parameters). Previously the alias
# rewrite bypassed the factory body entirely, leaving the captured
# locals nondet; now the body is run and each captured local is
# snapshotted at the call site so the escaping closure observes the
# correct value (the factory's final value of the local — which is
# also the late-binding value, since the factory has returned).


def f():
    x = 5
    return lambda: x


g = f()
assert g() == 5

# Captured value is computed from multiple locals.
def h():
    a = 10
    b = 5
    return lambda: a + b


gh = h()
assert gh() == 15

# A wrong value must still be reported (no false proof).
def k():
    v = 7
    return lambda: v


gk = k()
assert gk() != 6
