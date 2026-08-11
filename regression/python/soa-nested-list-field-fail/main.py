# Twins: an UNPROVEN inner index must fail its IndexError
# obligation, and a bare nested-list value escaping into a call
# must loud-fail (python-model-limitation), never silently box.
from typing import List, TypedDict


class Row(TypedDict):
    tags: List[int]


def sink(x) -> None:
    pass


def fetch() -> List[Row]: ...


xs = fetch()
if len(xs) > 2:
    v = xs[1]['tags'][2]
    sink(xs[0]['tags'])
