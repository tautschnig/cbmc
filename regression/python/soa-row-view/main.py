# SoA row-view binding: `r = xs[i]` freezes r as a ROW INDEX into
# xs's parallel field arrays (the comprehension-variable shape made
# persistent). Field reads route to f_data[r]; the index is frozen
# at bind time (PLR 3.1: r references the object, not the
# expression); negative indices normalize before the bounds
# obligation (PLR 6.10.2).
from typing import List, TypedDict


class Row(TypedDict):
    a: int
    b: int


def fetch() -> List[Row]: ...


xs = fetch()
if len(xs) > 3:
    r = xs[2]
    assert r['a'] == xs[2]['a']
    assert r['b'] == xs[2]['b']
    i = 1
    v = xs[i]
    i = 2
    assert v['a'] == xs[1]['a']
    last = xs[-1]
    assert last['a'] == xs[len(xs) - 1]['a']
