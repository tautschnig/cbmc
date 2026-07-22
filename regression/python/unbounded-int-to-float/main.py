# Int -> FP conversion under --python-unbounded-ints: true division and
# int/float mixing need a mathematical-Int-to-floatbv typecast, which the
# smt2 backend previously rejected ("Unknown typecast integer -> float").
# Lowered as ((_ to_fp e s) RNE (to_real <Int>)) -- to_real embeds Int in
# Real exactly.
b = 7 / 2
assert b == 3.5
c = -7 / 2
assert c == -3.5

x = 1.5
n = 2
assert x * n == 3.0


def f(v: float) -> float:
    return v + 0.5


assert f(1) == 1.5
