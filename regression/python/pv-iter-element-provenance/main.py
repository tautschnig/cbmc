# Per-instance ELEMENT provenance (plan §1 phase 2): the elements produced
# by a dispatched __iter__ carry their concrete values through the Any
# channel -- each in-bounds slot of the list[python_value] view is the
# WRAPPED dispatched element (out-of-bounds slots stay nondet, never read).
# Before: length was coupled but elements were wholly nondet, so the sum
# below was unprovable (sound false alarm).
from typing import Any


class Box:
    def __iter__(self):
        return iter([1, 2])


def get() -> Any:
    return Box()


r: Any = get()
total = 0
for v in r:
    total = total + v
assert total == 3
