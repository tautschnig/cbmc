# PLR §7.2.2: extended starred unpacking. The Tuple/List target
# can include a single Starred element which collects "the rest"
# of the rhs into a list. The non-starred elements before/after
# bind to fixed positions in the source.

def first_starred() -> None:
    first, *rest = [1, 2, 3]
    assert first == 1
    assert rest == [2, 3]


def starred_last() -> None:
    *head, last = [1, 2, 3]
    assert head == [1, 2]
    assert last == 3


def starred_middle() -> None:
    a, *mid, z = [1, 2, 3, 4, 5]
    assert a == 1
    assert mid == [2, 3, 4]
    assert z == 5


def empty_rest() -> None:
    head, *tail = [1]
    assert head == 1
    assert tail == []


def from_function_return() -> None:
    def gen():
        return [1, 2, 3, 4]
    a, *rest = gen()
    assert a == 1
    assert rest == [2, 3, 4]


first_starred()
starred_last()
starred_middle()
empty_rest()
from_function_return()
