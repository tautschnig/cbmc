# Per-instance class-identity soundness: a CLASS-tagged Any value whose
# instance is NOT of the dunder-owning class must NOT take the dispatch
# path -- CPython raises TypeError ('B' object is not iterable /
# subscriptable). A blanket tag == CLASS guard was a false proof for both
# __iter__ and __getitem__; the guard now reads __class_tag through
# __class_ptr (the same identity isinstance dispatches on).
from typing import Any


class A:
    def __iter__(self) -> list:
        return []

    def __getitem__(self, k):
        return 1


class B:
    def __init__(self) -> None:
        self.x = 1


def get() -> Any:
    return B()


r: Any = get()
for c in r:  # TypeError: 'B' object is not iterable
    pass
assert True
