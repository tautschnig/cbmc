# ONE lookup encoding for membership / subscript / get
# (dict_lookup_witness_members): each pair must AGREE on presence at
# every length. Before the unification, membership was a bounded
# 16-slot scan while the subscript witness was complete -- a
# membership-guarded read could false-alarm KeyError (present branch)
# and a NOT-in guard could meet a "found" get (absent branch).
from typing import Dict


def fetch() -> Dict[str, int]: ...


d = fetch()
if 'k' in d:
    x = d['k']       # membership proved presence: no KeyError
    assert x == x
if 'q' not in d:
    assert d.get('q', -1) == -1
