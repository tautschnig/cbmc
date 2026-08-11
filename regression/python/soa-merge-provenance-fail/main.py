# Twin (the m7 wrong-owner false proof): a row view whose OWNER
# differs across branches must NOT read either owner after the
# merge -- the binding drops at the merge, the name stays guarded,
# and the post-merge read fail-closes loudly. Before the fix,
# conversion-time binding kept whichever branch converted LAST, so
# BOTH paths read ys -- `r['a'] == ys[0]['a']` false-proved.
from typing import List, TypedDict


class Row(TypedDict):
    a: int


def fetch() -> List[Row]: ...


def flag() -> bool: ...


xs = fetch()
ys = fetch()
if len(xs) > 1 and len(ys) > 1:
    if flag():
        r = xs[0]
    else:
        r = ys[0]
    v = r['a']
    assert v == ys[0]['a']
