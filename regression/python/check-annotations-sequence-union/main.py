# --python-check-annotations precision: a list IS a Sequence, so a list argument
# to a `Sequence[str] | None` parameter must NOT be flagged. The union contains
# an Any-like component (the container ABCs Sequence/Iterable/Mapping are modelled
# as python_value), which accepts any value -- previously the strict-category
# union check rejected the list (sequence_2/3/4 false positive).
from typing import Sequence


def foo(s: Sequence[str] | None = None) -> None:
    if s is None:
        return


foo(None)
foo([])
foo(["a", "b", "c"])
