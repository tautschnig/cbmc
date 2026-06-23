# PLR 3.2 / 4.2.2: a function may call another function defined LATER
# (forward reference). The caller's return type must reflect the
# forward-referenced callee's return type, resolved before bodies are
# converted (sub-pass 1b.4 fixpoint), so equality checks are real runtime
# checks rather than wrongly-folded false alarms. Covers scalar, chained,
# and CONTAINER (dict/list/tuple) callee return types.
def f():
    return g()


def g():
    return "global"


def h():
    return f()


def fd():
    return gd()


def gd():
    return {"a": 1}


def fl():
    return gl()


def gl():
    return [1, 2, 3]


def ft():
    return gt()


def gt():
    return (1, 2)


assert f() == "global"
assert h() == "global"
assert fd() == {"a": 1}
assert fl() == [1, 2, 3]
assert ft() == (1, 2)
