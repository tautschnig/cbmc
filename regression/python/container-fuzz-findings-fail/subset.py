# The assertion fails in CPython.
# A set of strings <= set() (a set of ints) was always True.
def f() -> bool:
    s = {"a"}
    t = set()
    return s <= t


assert f(), "subset"
