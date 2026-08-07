# twin: an unguarded get on a symbolic dict must keep BOTH failure
# directions -- key may be present with a non-default value.
from typing import Dict


def fetch() -> Dict[str, int]: ...


d = fetch()
assert d.get('k', -1) == -1
