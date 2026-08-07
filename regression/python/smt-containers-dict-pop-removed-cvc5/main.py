# pop REMOVES the key: needs the stub-dict key-uniqueness invariant
# (a real dict cannot hold a key twice) plus the quantified
# compaction. The entailment (uniqueness + only-w-matched =>
# forall q. keys'[q] != k) needs quantifier instantiation Z3's
# trigger inference misses without :pattern annotations; cvc5
# proves it in seconds (recorded in the plan doc).
from typing import Dict


def fetch() -> Dict[str, int]: ...


d = fetch()
if 'k' in d:
    d.pop('k')
    assert 'k' not in d
