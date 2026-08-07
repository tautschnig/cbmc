# twin: after inserting into a SYMBOLIC dict the length is len0 or
# len0+1 -- a fixed-length claim must fail.
from typing import Dict


def fetch() -> Dict[str, int]: ...


d = fetch()
d['k'] = 42
assert len(d) == 1
