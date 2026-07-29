# Controls for the undefined-name NameError modeling: legitimate reads
# must stay verifiable.
from datetime import datetime, timezone


def comp_and_for_leak():
    xs = [1, 2, 3]
    ys = [r * 2 for r in xs]
    assert len(ys) == 3
    total = 0
    for r in xs:
        total += r
    # PLR §8.3: a for-loop target DOES remain bound after the loop.
    assert r == 3
    assert total == 6


def walrus_leak():
    # PEP 572: a walrus inside a comprehension binds in the enclosing
    # scope.
    w = [y := 5 for _ in [0]]
    assert y == 5


def explicit_import():
    return int(datetime.now(timezone.utc).timestamp())


comp_and_for_leak()
walrus_leak()
explicit_import()
