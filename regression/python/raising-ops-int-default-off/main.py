# Without --python-raising-ops-check, int(str) is modeled as silently
# succeeding (precision-favoring default), so this verifies.
def get() -> str: ...


def f() -> None:
    s = get()
    n: int = int(s)
    assert n == n


f()
