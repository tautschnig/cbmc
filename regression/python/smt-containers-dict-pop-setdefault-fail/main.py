# twins: unguarded pop keeps its KeyError obligation; unguarded
# setdefault's result is not provably the default (key may be
# present with another value).
from typing import Dict


def fetch() -> Dict[str, int]: ...


d = fetch()
assert d.setdefault('k', -1) == -1
