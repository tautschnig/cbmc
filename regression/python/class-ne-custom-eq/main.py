# PLR §3.3.1: `!=` on a class instance with a custom __eq__ must compare
# by value (negate __eq__), not by struct layout.
class Money:
    def __init__(self, cents: int):
        self.cents = cents

    def __eq__(self, other) -> bool:
        return self.cents == other.cents


a = Money(100)
b = Money(100)
c = Money(200)
assert a == b
assert not (a != b)
assert a != c
assert not (a == c)
