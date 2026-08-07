# Store-side capacity lift (--python-smt-containers): d[k] = v goes
# through ONE choke point (emit_dict_store) -- witness replace /
# capacity-FREE length-indexed insert into the infinite arrays. A
# 20-key literal is fully representable (the construction cut is
# bounded-mode-only), and stores on a SYMBOLIC dict are visible to
# every later lookup at any length.
from typing import Dict


def fetch() -> Dict[str, int]: ...


d = {'k0': 0, 'k1': 1, 'k2': 2, 'k3': 3, 'k4': 4, 'k5': 5, 'k6': 6, 'k7': 7, 'k8': 8, 'k9': 9, 'k10': 10, 'k11': 11, 'k12': 12, 'k13': 13, 'k14': 14, 'k15': 15, 'k16': 16, 'k17': 17, 'k18': 18, 'k19': 19}
assert d['k18'] == 18

s = fetch()
s['k'] = 42
assert s['k'] == 42
assert 'k' in s
n = len(s)
s['k'] = 7
assert s['k'] == 7
assert len(s) == n
