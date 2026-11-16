def nondet_int() -> int: ...


def gate(v: int) -> None:
    assert 0 < v
    assert v < 8


n = nondet_int()
ok = True
try:
    gate(n)
except AssertionError:
    ok = False
assert ok == (0 < n < 9)
