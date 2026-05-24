# PLR §8.7: decorators defined inside another function (e.g. a
# test harness wrapping its own decorator + decorated function)
# must dispatch through the wrapper just like module-level
# decorators. Two pieces:
#   - the AST search for the decorator's FunctionDef must
#     recurse into nested function/class/control-flow bodies
#     (not just scan module body)
#   - the function_aliases entry must be set under BOTH the
#     unqualified "python::f" and the qualified
#     "python::<scope>::f" keys, since the call-site lookup
#     uses qualify_name() which depends on context.

def harness() -> None:
    def my_dec(fn):
        def wrapper(*a):
            return fn(*a) + 1
        return wrapper

    @my_dec
    def f(x: int) -> int:
        return x

    assert f(10) == 11
    assert f(0) == 1


def harness2() -> None:
    # Multiple calls through the decorator inside the same
    # function — the alias should resolve every time.
    def double_dec(fn):
        def wrapper(*a):
            return fn(*a) * 2
        return wrapper

    @double_dec
    def g(x: int) -> int:
        return x + 1

    assert g(5) == 12
    assert g(0) == 2


harness()
harness2()
