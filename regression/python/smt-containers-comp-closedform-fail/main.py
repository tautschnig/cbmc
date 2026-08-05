# twin: the mapped value must be the REAL one
def fetch() -> list: ...


xs = fetch()
ys = [x * 2 for x in xs]
if len(xs) > 1:
    assert ys[0] == xs[0] * 2 + 1
