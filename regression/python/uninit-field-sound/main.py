# Companion to uninit-field-attribute-error: a field unconditionally
# assigned in __init__, and a conditionally-declared field read only on
# the path where it was assigned, must NOT raise AttributeError.
class P:
    x: int
    y: int

    def __init__(self, flag: bool) -> None:
        self.x = 1
        if flag:
            self.y = 2


p = P(True)
assert p.x == 1
assert p.y == 2
