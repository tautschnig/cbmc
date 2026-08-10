# Escape twin: a BARE use of a row view outside a field subscript
# would leak the row INDEX as the element VALUE (the demonstrated
# index-pun false-proof class) -- it must loud-fail via the
# python-model-limitation property, and an opaque call over the
# owner must stale the view fail-closed.
from typing import List, TypedDict


class Row(TypedDict):
    a: int


def fetch() -> List[Row]: ...


def sink(x) -> None:
    pass


xs = fetch()
if len(xs) > 1:
    r = xs[0]
    sink(r)
    assert r['a'] == xs[0]['a']
