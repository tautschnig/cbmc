# differential2 §11b: __getattr__ is CPython's fallback hook, called
# when normal attribute lookup fails. A read of an attribute that is
# not a field/property/method now dispatches to __getattr__ (resolved
# via the MRO, so an inherited __getattr__ works too) instead of
# returning a nondet over-approximation.
class C:
    def __init__(self) -> None:
        self.x = 1

    def __getattr__(self, name: str) -> int:
        return 42


class Sub(C):
    pass


c = C()
assert c.x == 1
assert c.missing == 42
assert Sub().anything == 42
