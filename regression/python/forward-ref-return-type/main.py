# PLR 3.2 / 4.2.2: a function may call another function defined LATER
# (forward reference). The caller's return type must reflect the
# forward-referenced callee's return type, resolved before bodies are
# converted (sub-pass 1b fixpoint), so `f() == "global"` is a real
# runtime check rather than a wrongly-folded false alarm.
def f():
    return g()


def g():
    return "global"


# chain through a third forward-referenced function
def h():
    return f()


assert f() == "global"
assert h() == "global"
