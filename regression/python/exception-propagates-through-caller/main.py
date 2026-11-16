log = []


def g() -> None:
    raise KeyError()


def f() -> None:
    g()
    log.append(1)      # never runs: g raised


try:
    f()
except KeyError:
    pass
assert len(log) != 0   # false in CPython
