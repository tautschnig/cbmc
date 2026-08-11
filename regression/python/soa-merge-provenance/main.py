# Merge-safe SoA provenance: bindings that AGREE across branches
# survive the merge (same owner, path-sensitive index stays exact);
# rebinds to the same stub shape keep working.
from typing import List, TypedDict


class Row(TypedDict):
    a: int


def fetch() -> List[Row]: ...


def flag() -> bool: ...


xs = fetch()
if flag():
    xs = fetch()
if len(xs) > 2:
    if flag():
        r = xs[0]
    else:
        r = xs[1]
    assert r['a'] == r['a']
