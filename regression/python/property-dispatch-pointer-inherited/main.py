# differential2 §11b (@property dispatch beyond by-value/own-class).
# Three cases that previously fell through to a field read / nondet:
#   * a property accessed through a class-typed parameter (pointer recv)
#   * a property accessed through self inside a method (pointer recv)
#   * a property inherited from a base class
class Box:
    def __init__(self, n: int) -> None:
        self._n = n

    @property
    def n2(self) -> int:
        return self._n * 2

    def via_self(self) -> int:
        return self.n2 + 1


class Sub(Box):
    pass


def via_param(b: Box) -> int:
    return b.n2


assert via_param(Box(5)) == 10
assert Box(5).via_self() == 11
assert Sub(7).n2 == 14
