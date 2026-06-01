# §10: a callable bound to different functions in different branches
# must dispatch path-sensitively. Previously the conversion-time
# function_aliases map kept only the last branch's target (bf), so
# pick(True) wrongly resolved to bf. A runtime tag set per branch now
# selects the correct callee.


def af() -> int:
    return 1


def bf() -> int:
    return 2


def pick(cond: bool) -> int:
    if cond:
        h = af
    else:
        h = bf
    return h()


assert pick(True) == 1
assert pick(False) == 2


def add(a: int, b: int) -> int:
    return a + b


def sub(a: int, b: int) -> int:
    return a - b


def op(cond: bool) -> int:
    if cond:
        f = add
    else:
        f = sub
    return f(10, 3)


assert op(True) == 13
assert op(False) == 7
