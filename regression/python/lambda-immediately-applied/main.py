# PLR §6.14: an immediately-applied lambda (lambda ...: ...)(args) must be
# called, not left nondet. Covers arithmetic, multiple parameters, and
# attribute access / method dispatch on an object argument.
class A:
    def __init__(self, v: int) -> None:
        self.v = v

    def doubled(self) -> int:
        return self.v * 2


def t() -> None:
    assert (lambda x: x + 1)(5) == 6
    assert (lambda a, b: a * b)(3, 4) == 12
    assert (lambda o: o.v)(A(7)) == 7
    assert (lambda o: o.doubled())(A(7)) == 14


t()
