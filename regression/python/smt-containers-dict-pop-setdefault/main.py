# setdefault / pop through the shared quantified lookup witness
# (--python-smt-containers): complete at every length. pop's removal
# is an EXACT lambda-array compaction preserving insertion order
# (PLR 3.7+). Absent-key setdefault inserts and the read-back sees
# it; present-key pop returns the stored value and decrements len.
from typing import Dict


def fetch() -> Dict[str, int]: ...


d = fetch()
if 'k' not in d:
    r = d.setdefault('k', -1)
    assert r == -1
    assert d['k'] == -1
    assert 'k' in d
e = fetch()
if 'q' in e:
    n = len(e)
    v = e['q']
    assert e.pop('q') == v
    assert len(e) == n - 1
if 'z' not in e:
    assert e.pop('z', -7) == -7
