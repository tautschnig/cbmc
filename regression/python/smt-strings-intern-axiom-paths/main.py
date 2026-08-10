# The interned-constant strtab axiom must hold on EVERY path that
# uses the handle: a once-per-intern emission landed in whichever
# branch interned first, leaving the OTHER branch's reads
# unconstrained (strtab(id) was ""), so d.get('k') on the else-path
# could "find" a key the membership lift said was absent -- the two
# encodings disagreed. Axioms are now re-emitted at every intern
# use (duplicate assumes are harmless).
from typing import Dict


def fetch() -> Dict[str, int]: ...


d = fetch()
if 'k' in d:
    x = d.get('k', -1)
    y = d['k']
    assert x == y
else:
    assert d.get('k', -1) == -1
