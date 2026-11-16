box = [0]


def f(k: int) -> int:
    if k > 0:
        raise KeyError()
    return 1


def g() -> None:
    box[0] = f(1) + 10      # must not store: f raised


try:
    g()
except KeyError:
    pass
assert box[0] == 0
