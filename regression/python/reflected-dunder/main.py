# PLR 3.3.1: reflected-operand fallback for ordering
# dunders. When the left operand has no __lt__ / __le__ /
# __gt__ / __ge__, Python tries the right operand's
# reflected method: __gt__ reflects to __lt__, __ge__ to
# __le__, and vice versa.


# Only __gt__ defined.
class OnlyGt:
    def __init__(self, v: int) -> None:
        self.v = v

    def __gt__(self, other: "OnlyGt") -> bool:
        return self.v > other.v


a = OnlyGt(5)
b = OnlyGt(10)

# a < b: left has no __lt__; fallback to right's __gt__: b > a.
assert a < b
assert not (b < a)
assert not (a < a)


# Only __le__ defined. Verify > reflects to __lt__ direction.
class OnlyLe:
    def __init__(self, v: int) -> None:
        self.v = v

    def __le__(self, other: "OnlyLe") -> bool:
        return self.v <= other.v


p = OnlyLe(3)
q = OnlyLe(8)

# p >= q: left has no __ge__; fallback to right's __le__: q <= p.
# q <= p means 8 <= 3 → False. So p >= q is False.
assert not (p >= q)
# q >= p: right's __le__ = p.__le__(q) = 3 <= 8 = True.
assert q >= p
