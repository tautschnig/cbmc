# int() of a non-constant string can raise ValueError. Under
# --python-raising-ops-check this is modeled as may-raise, so the
# uncaught path is reachable and verification FAILS (matching CPython,
# where a non-numeric input raises ValueError).
def get() -> str: ...


def f() -> None:
    s = get()
    n: int = int(s)  # may raise ValueError
    assert n == n


f()
