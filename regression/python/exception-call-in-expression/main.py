log = []


def f(k: int) -> int:
    if k > 0:
        raise KeyError()
    return k


def note(v: int) -> int:
    log.append(v)
    return v


def g1() -> None:
    note(f(1))                   # note must not run


def g2() -> None:
    if f(1) == 0:                # condition raises: body skipped
        log.append(9)
    log.append(8)


def g3(k: int) -> bool:
    return k == 0 or f(k) == 0   # short circuit; f raises for k == 1


for g in (0, 1, 2):
    try:
        if g == 0:
            g1()
        elif g == 1:
            g2()
        else:
            g3(0)
            g3(1)
            log.append(7)
    except KeyError:
        pass
assert log == []
