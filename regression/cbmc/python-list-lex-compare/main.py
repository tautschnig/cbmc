# PLR §6.10.1: list lexicographic ordering. Compare elements
# left-to-right; first differing pair decides; if one is a
# prefix of the other, the shorter is less.

def ints() -> None:
    a = [1, 2, 3]
    b = [1, 2, 4]
    assert a < b
    assert b > a
    assert a <= b
    assert b >= a
    assert a <= [1, 2, 3]
    assert a >= [1, 2, 3]


def prefix() -> None:
    assert [1, 2] < [1, 2, 3]
    assert [1, 2, 3] > [1, 2]
    empty: list = []
    assert empty < [1]


def floats() -> None:
    assert [1.0, 2.0] < [1.0, 3.0]
    assert [1.5, 2.5] > [1.5, 1.0]


def cross_type_int_float() -> None:
    a = [1, 2]
    b = [1.0, 3.0]
    assert a < b
    assert b > a


def bool_int() -> None:
    # PLR §3.2.1: bool is a subtype of int (True == 1, False == 0)
    assert [True] < [2]
    assert [False] < [1]


def strings() -> None:
    assert ['A', 'B', 'C'] < ['A', 'B', 'C', 'D']
    assert ['A', 'C'] > ['A', 'B']


ints()
prefix()
floats()
cross_type_int_float()
bool_int()
strings()
