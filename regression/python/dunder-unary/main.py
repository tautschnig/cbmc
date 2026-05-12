# PLR §3.3.8 data model — unary-operator dunders.
# '-x' / '+x' / '~x' on class instances dispatch to
# x.__neg__() / x.__pos__() / x.__invert__().

class Num:
    v: int

    def __init__(self, v: int) -> None:
        self.v = v

    def __neg__(self) -> int:
        return -self.v

    def __pos__(self) -> int:
        return self.v

    def __invert__(self) -> int:
        return ~self.v


n = Num(5)
assert -n == -5
assert +n == 5
assert ~n == -6

m = Num(-3)
assert -m == 3
assert ~m == 2
