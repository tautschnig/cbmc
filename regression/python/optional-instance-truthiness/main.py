# PLR §6.6: a class instance is truthy. An Optional[instance] result
# (X | None) carrying an instance must be truthy in a boolean context;
# previously `not r` unwrapped the tagged-union to its int slot and
# wrongly treated the present instance as falsy (re.match's Match|None).
from typing import Optional


class M:
    pass


def make(present: bool) -> Optional[M]:
    if present:
        return M()
    return None


def f() -> None:
    r = make(True)
    assert r is not None
    assert not (not r)  # r is truthy -> `not r` is False
    n = make(False)
    assert not n  # None is falsy


f()
