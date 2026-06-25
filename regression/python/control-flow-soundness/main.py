# Soundness (P2 audit lock-in): exception / control-flow paths must not let a
# post-path property be wrongly proved. Each assertion is genuinely FALSE.
x = 0
try:
    raise ValueError("a")   # not caught by KeyError -> propagates
except KeyError:
    x = 1
except ValueError:
    x = 3
assert x == 1               # x is 3 (ValueError branch), not 1

def f():
    try:
        return 1
    finally:
        return 2            # finally return overrides
assert f() == 1             # f() is 2
