# int() of a symbolic float raises for an infinity or a NaN.
f = nondet_float()
try:
    n = int(f)
    assert f == f and f != float("inf") and f != float("-inf")
except OverflowError:
    assert f == float("inf") or f == float("-inf")
except ValueError:
    assert f != f
