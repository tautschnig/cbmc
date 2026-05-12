# PLR §3.3.5 data model — callable instances via __call__.
# 'inst(...)' dispatches to inst.__call__(...)
# when inst's class defines a __call__ method.

class Adder:
    base: int

    def __init__(self, base: int) -> None:
        self.base = base

    def __call__(self, x: int) -> int:
        return self.base + x


add5 = Adder(5)
assert add5(3) == 8
assert add5(10) == 15

add100 = Adder(100)
assert add100(0) == 100
assert add100(-1) == 99
