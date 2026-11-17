from collections import Counter


def nondet_int() -> int: ...


a = nondet_int()
b = nondet_int()
c = Counter([a, b, a])
assert c[a] >= 2
assert len(c) == (1 if a == b else 2)
