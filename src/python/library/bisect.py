"""
Verification model of the `bisect` module.

Binary-search insertion position / insertion functions for
sorted sequences. We use simple linear scans — slower than
CPython's binary search but semantically equivalent — because
verification code typically works with small lists where the
difference doesn't matter, and linear scans are easier for
CBMC to reason about symbolically.
"""


def bisect_left(a, x, lo=0, hi=None):
    if hi is None:
        hi = len(a)
    if lo < 0:
        raise ValueError("lo must be non-negative")
    i = lo
    while i < hi:
        if a[i] < x:
            i = i + 1
        else:
            break
    return i


def bisect_right(a, x, lo=0, hi=None):
    if hi is None:
        hi = len(a)
    if lo < 0:
        raise ValueError("lo must be non-negative")
    i = lo
    while i < hi:
        if x < a[i]:
            break
        i = i + 1
    return i


# Aliases per CPython.
bisect = bisect_right


def insort_left(a, x, lo=0, hi=None):
    i = bisect_left(a, x, lo, hi)
    a.insert(i, x)
    return None


def insort_right(a, x, lo=0, hi=None):
    i = bisect_right(a, x, lo, hi)
    a.insert(i, x)
    return None


insort = insort_right
