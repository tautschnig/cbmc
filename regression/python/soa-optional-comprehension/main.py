# Optional-field comprehension, EXACT: the presence-guarded
# KeyError from the subscript hook RELOCATES to a representative
# in-range index (the comprehension evaluates the body at every
# row, so it raises iff it raises at SOME row) instead of forcing
# the loud bounded fallback. Zero rows = vacuously safe.
from typing import List, TypedDict
from typing_extensions import NotRequired


class Row(TypedDict):
    a: int
    b: NotRequired[int]


def fetch() -> List[Row]: ...


xs = fetch()
ys = [r['a'] for r in xs]
assert len(ys) == len(xs)
if len(xs) == 0:
    zs = [r['b'] for r in xs]
    assert len(zs) == 0
