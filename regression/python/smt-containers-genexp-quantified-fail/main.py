# twin: the quantifier must SEE a known violating element
def fetch() -> list[int]: ...


xs = fetch()
if len(xs) > 2 and xs[1] == -5:
    assert all(x > 0 for x in xs)
