# --python-missing-return-check must NOT fire when every path returns a value.
def f(x: int) -> int:
    if x > 0:
        return x
    return 0

assert f(-1) == 0
