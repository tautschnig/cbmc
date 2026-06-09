# A class instance assigned to a tagged-union (str | datetime) variable must
# be wrapped (CLASS tag + __class_ptr) so later attribute reads through the
# union resolve to the instance. Previously the class was constructed in
# place on the union slot, running __init__ on a nondet self, leaving the
# instance uninitialised.
from datetime import datetime


def foo(x: str | datetime) -> tuple[int, int, int]:
    if isinstance(x, datetime):
        return (x.year, x.month, x.day)
    else:
        return (0, 0, 0)


d: str | datetime
d = datetime(2, 3, 4)
assert foo(d) == (2, 3, 4)
