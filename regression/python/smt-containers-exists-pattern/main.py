# Selective :pattern emission (EXISTS only): 17 stacked membership
# conjuncts were a hard Z3 TIMEOUT; with the E-matching hint on the
# exists encodings the query proves in ~20s. FORALL witnesses stay
# unannotated (a pattern RESTRICTS instantiation; Z3's saturation is
# what solves the mutation chains -- the blanket-emission regression).
from typing import Dict


def fetch() -> Dict[str, int]: ...


d = fetch()
if 'k0' in d and 'k1' in d and 'k2' in d and 'k3' in d and 'k4' in d and 'k5' in d and 'k6' in d and 'k7' in d and 'k8' in d and 'k9' in d and 'k10' in d and 'k11' in d and 'k12' in d and 'k13' in d and 'k14' in d and 'k15' in d and 'k16' in d:
    x = d['k16']
    assert x == x
