# Native SMT-String backend: kwargs-dict KEY packing must box the key
# string behind the typed pointer (coerce_element), not store the raw
# string struct -- a raw smt_string in a boxed-key-typed array made the
# byte-imaged skeleton variable-width and crashed core CBMC
# (simplify_rec type postcondition; 32 of the 51 corpus programs).
from typing import Any


class Svc:
    def op(self, **kwargs: Any) -> int:
        if "Name" in kwargs:
            return 1
        return 0


def get() -> Any:
    return Svc()


c: Any = get()
r: Any = c.op(Name="x")
assert r == 1
