# Native SMT-String backend, STRING-ID HANDLES (strings plan 2026-07-20):
# class str fields are fixed-width int64 handles whose string denotation
# is strtab(h) -- an uninterpreted bv64 -> String function declared
# solver-side (find_symbols emits the declare-fun; the generic
# function-application path applies it: ZERO backend changes). Aggregates
# stay fixed-width, so the *(int32*)__class_ptr identity reads (isinstance
# below) no longer byte-image a variable-width struct (the unpack_struct
# TOERR family) -- and unlike POINTER boxing (reverted), byte-copying a
# handle is always well-defined: no deref exists to byte-extract.
# Values round-trip through store (ctor + reassignment) and read.
from typing import Any


class C:
    name: str
    region: str

    def __init__(self) -> None:
        self.name = "abc"
        self.region = "eu"


def f() -> Any:
    return C()


r: Any = f()
if isinstance(r, C):
    pass
c = C()
assert len(c.name) == 3
assert c.region == "eu"
c.name = "wxyz"
assert len(c.name) == 4
