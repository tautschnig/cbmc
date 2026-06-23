# PLR 5.9: an operand is evaluated once. A python_value-returning call that
# mutates a by-reference list must not be re-evaluated by the `==` lowering
# (tag + value); otherwise the two evaluations diverge.
def f(l: list) -> int:
    item = l[0]
    del l[0]
    return item


h = [1, 2, 3]
assert f(h) == 1   # f evaluated once -> returns the original first element
