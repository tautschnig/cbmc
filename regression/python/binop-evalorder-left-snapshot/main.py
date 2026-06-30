# PLR 6.16: operands evaluate left-to-right. `x + g()` reads x BEFORE g() runs,
# so when g() rebinds the global x the result uses the OLD x. Here r == 1 (not
# 100), so `assert r == 100` raises AssertionError.
x = 1


def g() -> int:
    global x
    x = 100
    return 0


r = x + g()
assert r == 100
