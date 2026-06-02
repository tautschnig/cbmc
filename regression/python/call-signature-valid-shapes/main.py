# Valid call shapes must NOT be flagged: *args, **kwargs, defaults,
# keyword-only, and keyword matching a parameter.
def va(*a):
    return len(a)


def kw(**k):
    return 0


def deflt(a, b=2):
    return a + b


def kwonly(a, *, k):
    return a + k


assert va(1, 2, 3) == 3
assert kw(x=1, y=2) == 0
assert deflt(1) == 3
assert kwonly(1, k=2) == 3
assert deflt(a=5, b=6) == 11
