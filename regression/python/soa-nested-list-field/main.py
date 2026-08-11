# Nested list fields in SoA rows: List[TD] where TD carries a
# List[<scalar>] field lowers the field to a per-field MATRIX
# (rows x elements) plus a per-row length array. Safe consumers:
# len(xs[i]['f']) -> f_len[i]; xs[i]['f'][j] -> f_data[i][j] with
# the element IndexError against f_len[i]; row-view forms agree.
from typing import List, TypedDict


class Row(TypedDict):
    a: int
    tags: List[int]


def fetch() -> List[Row]: ...


xs = fetch()
if len(xs) > 2:
    assert xs[1]['a'] == xs[1]['a']
    n = len(xs[1]['tags'])
    assert n >= 0
    if n > 3:
        v = xs[1]['tags'][2]
        assert v == xs[1]['tags'][2]
    r = xs[1]
    if len(r['tags']) > 3:
        assert r['tags'][2] == xs[1]['tags'][2]
