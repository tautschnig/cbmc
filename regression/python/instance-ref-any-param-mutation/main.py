# PLR §3.1: an instance passed to an UNANNOTATED (Any) parameter preserves
# identity via the python_value{CLASS, address-of} boundary path. A mutation
# through it is visible to the caller. Regression guard.
class V:
    def __init__(self) -> None:
        self.x: int = 1


def g(t) -> None:
    t.x = 99


v = V()
g(v)
assert v.x == 99
