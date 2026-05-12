# PLR §3.3.1 / §3.3.8 data model — binary-operator
# dunders. 'a OP b' where a is a user-defined class
# instance with the corresponding __<op>__ method
# dispatches to a.__<op>__(b). Covered here: __add__,
# __sub__, __mul__, __floordiv__, __mod__.

class Counter:
    n: int

    def __init__(self, n: int) -> None:
        self.n = n

    def __add__(self, other: int) -> int:
        return self.n + other

    def __sub__(self, other: int) -> int:
        return self.n - other

    def __mul__(self, other: int) -> int:
        return self.n * other

    def __floordiv__(self, other: int) -> int:
        return self.n // other

    def __mod__(self, other: int) -> int:
        return self.n % other


c = Counter(10)
assert c + 5 == 15
assert c - 3 == 7
assert c * 2 == 20
assert c // 3 == 3
assert c % 3 == 1
