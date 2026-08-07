# KNOWNBUG: reading a container VALUE out of a stub-returned TypedDict
# and taking len() -- the boxed value's dereference loses the heap
# object through the infinite values array (the P3a deref-precision
# residual, same class as smt-containers-nested's inner-read note),
# so the len >= 0 assume on the stub object is not connected. LOUD
# (false alarm), never a false proof. Flip to CORE when the
# value-set precision for boxed values in infinite arrays lands.
from typing import List, TypedDict


class Resp(TypedDict):
    items: List[int]
    count: int


def fetch() -> Resp: ...


r = fetch()
xs = r['items']
assert len(xs) >= 0
