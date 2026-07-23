# No-false-positive guard: a symbol REBOUND away from None must not be
# flagged (none_constants cleared on rebind), and == / is comparisons
# with None are always valid.
import random


def rebind() -> None:
    x = None
    x = 5
    assert x < 10  # x is int here, not None


def eq_ok() -> None:
    y = None
    assert y == None
    assert y is None


def merged() -> None:
    z = None
    if random.randint(0, 1):
        z = 7
    if z is not None:
        assert z < 100  # guarded; z is int on this path


rebind()
eq_ok()
merged()
