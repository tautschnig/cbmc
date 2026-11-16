calls = []


def f() -> int:
    calls.append(1)
    return 0


d = {0: 'a'}
v = d[f()]
assert v == 'a'
assert len(calls) == 1
