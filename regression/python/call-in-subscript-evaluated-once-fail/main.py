calls = []


def f() -> int:
    calls.append(1)
    return 0


xs = [10, 20]
v = xs[f()]
assert v == 10
assert len(calls) == 2
