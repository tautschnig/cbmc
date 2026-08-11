# Twin: an optional field read over rows of UNKNOWN presence may
# raise KeyError -- the relocated representative obligation must
# FIRE (this refutes both a silent-success and a lost-obligation
# regression).
from typing import List, TypedDict
from typing_extensions import NotRequired


class Row(TypedDict):
    a: int
    b: NotRequired[int]


def fetch() -> List[Row]: ...


xs = fetch()
ys = [r['b'] for r in xs]
assert len(ys) == len(xs)
