# Native backend, container completion: dict[str, str] VALUES are
# string-id handles (python_dict_type enforces the representation
# invariant; keys migrated from pointer boxing to handles too -- the
# pointer box byte-extracted the pointed string when value sets failed
# to resolve). Values round-trip through literal construction, subscript
# read, and subscript store.
from typing import Any, Dict


def f() -> Any:
    return {"a": "xy", "b": "z"}


d: Any = f()
if d:
    pass
d2: Dict[str, str] = {"k": "vv"}
assert len(d2["k"]) == 2
d2["k"] = "www"
assert len(d2["k"]) == 3
