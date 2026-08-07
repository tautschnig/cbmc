# len() of a container value read out of a stub-returned TypedDict.
# The boxed value's dereference can lose the heap object through the
# infinite values array (P3a deref-precision class), contributing a
# junk arm with an UNCONSTRAINED (even negative) length; the len()
# read boundary now reasserts the representation invariant
# len >= 0 (PLR 6.10: never negative -- CPython raises for a
# negative __len__), sound and precision-restoring (perf-study
# u2_srclen / t3_neglen).
from typing import List, TypedDict


class Resp(TypedDict):
    items: List[int]
    count: int


def fetch() -> Resp: ...


r = fetch()
xs = r['items']
assert len(xs) >= 0
