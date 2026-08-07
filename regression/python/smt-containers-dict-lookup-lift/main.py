# Quantified dict-key lookup (--python-smt-containers): storage is
# unbounded but the lookup was a 16-slot ite chain -- an entry the
# solver placed beyond the model bound was unreachable and a GUARDED
# read false-alarmed KeyError. The witness encoding (exists + first-
# occurrence witness) is complete at every length: presence proved
# by membership carries to the lookup over the same quantified
# domain. 10 distinct present keys force len >= 10; the guarded read
# must not raise.
from typing import Dict


def fetch() -> Dict[str, int]: ...


d = fetch()
if 'k0' in d and 'k1' in d and 'k2' in d and 'k3' in d and 'k4' in d and 'k5' in d and 'k6' in d and 'k7' in d and 'k8' in d and 'k9' in d:
    x = d['k9']
    assert x == x
