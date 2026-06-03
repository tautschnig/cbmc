# PLR §3.1: a class instance passed to a by-reference method parameter
# must work for any argument form. A non-lvalue argument (here the result
# of a function call) cannot be addressed directly; the method call path
# materialises it into a temp before taking its address. Without that,
# this raised "address_arithmetic does not handle struct".
class Box:
    v: int

    def __init__(self, x: int = 0):
        self.v = x


def make() -> Box:
    return Box(5)


class C:
    def take(self, b: Box) -> int:
        return b.v


c = C()
assert c.take(make()) == 5
