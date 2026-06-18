# Closure cell substrate (PLR §4.2.2), mutating slice. An escaping
# nested function that mutates a nonlocal variable shares a per-
# invocation HEAP CELL with the enclosing frame: the cell persists
# across calls (so a counter advances) and distinct factory
# invocations get INDEPENDENT cells (so two counters don't alias).
# Soundness-critical: per-invocation allocation prevents false proofs
# for multiple closures created from the same factory.


def make_counter():
    c = 0

    def inc():
        nonlocal c
        c += 1
        return c

    return inc


# Single counter: the cell persists across calls.
g = make_counter()
assert g() == 1
assert g() == 2
assert g() == 3

# Two counters are INDEPENDENT (distinct per-invocation cells).
a = make_counter()
b = make_counter()
assert a() == 1
assert b() == 1
assert a() == 2
assert b() == 2
assert a() == 3

# Independence holds even when one is advanced before the other exists.
c1 = make_counter()
c1()
c1()
c2 = make_counter()
assert c2() == 1
assert c1() == 3


# Accumulator: nonlocal mutation with an argument.
def make_acc():
    total = 0

    def add(n):
        nonlocal total
        total += n
        return total

    return add


acc = make_acc()
assert acc(10) == 10
assert acc(5) == 15
assert acc(100) == 115
