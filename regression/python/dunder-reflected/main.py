# PLR §3.3.1 reflected binary-operator dunders.
# When 'left OP right' where left doesn't support OP
# but right is a class with __r<op>__ method, Python
# dispatches to right.__r<op>__(left).
# Example: 5 + c where c is a Counter — Python tries
# int.__add__(Counter) (fails) → Counter.__radd__(5).
#
# This also prevents CBMC from crashing on 'int OP
# class' mixed-type arithmetic that its solver layers
# reject.

class Counter:
    n: int

    def __init__(self, n: int) -> None:
        self.n = n

    def __radd__(self, other: int) -> int:
        return self.n + other + 1000

    def __rmul__(self, other: int) -> int:
        return self.n * other * 2

    def __rsub__(self, other: int) -> int:
        return other - self.n - 100


c = Counter(10)
assert 5 + c == 1015   # 10 + 5 + 1000
assert 3 * c == 60     # 3 * 10 * 2
assert 200 - c == 90   # 200 - 10 - 100
