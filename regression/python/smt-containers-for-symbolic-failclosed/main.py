# A plain for-loop over a SYMBOLIC-length container under
# --python-smt-containers must NOT diverge in symex (it has no
# static trip count; this was the perf-study's ex3/ex4 silent wall,
# the for-lowering sibling of the comprehension fallback). The
# fail-closed bound terminates the loop and reports the truncation
# loudly via the python-model-bound property.
from typing import List


def fetch() -> List[int]: ...


xs = fetch()
total = 0
for x in xs:
    total = total + 1
assert total >= 0
