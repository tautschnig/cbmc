# twin: an UNGUARDED read of a symbolic dict must keep its KeyError
# obligation (the exists can be false), and the read value is
# unconstrained -- both directions must fail.
from typing import Dict


def fetch() -> Dict[str, int]: ...


d = fetch()
x = d['k']
assert x == 1
