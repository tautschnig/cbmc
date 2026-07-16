# PLR §6.13 iterability obligation on python_value iterables: iterating a
# value whose runtime tag is not iterable (here an int behind Any) raises
# TypeError. Before the obligation the list-slot deref silently iterated
# garbage (a false proof). Catchable per PLR §8.4 when a handler covers
# TypeError (see pv-wrong-class-dunder-typeerror for the definite form).
from typing import Any


def get() -> Any:
    return 5


r: Any = get()
ok = 0
try:
    for c in r:
        pass
except TypeError:
    ok = 1
assert ok == 1
