# PLR §6.2.4: generator expressions iterate only over the
# iterable's actual values. Pending property checks emitted while
# converting the element expression (e.g. ZeroDivisionError on
# `1 % x`) must be guarded by `i < length`, otherwise they fire
# for buffer slots beyond the iterable's actual length where the
# zero-init buffer makes x = 0, producing spurious exceptions on
# empty lists.

def all_empty_div() -> None:
    xs: list[int] = []
    # 1 % x would raise ZeroDivisionError if x = 0, but xs is
    # empty so the generator yields nothing — all() returns True.
    assert all(1 % x for x in xs)


def all_with_zeros() -> None:
    # When the list actually contains 0, the ZeroDivisionError
    # IS legitimate and must still be detected.
    xs = [1, 2, 3]
    assert all(1 % x for x in xs)


def any_empty_div() -> None:
    xs: list[int] = []
    # any(...) over an empty iterable is False.
    assert not any(1 % x for x in xs)


def all_literal_empty() -> None:
    assert all(1 % x for x in [])


all_empty_div()
all_with_zeros()
any_empty_div()
all_literal_empty()
