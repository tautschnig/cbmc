# A lambda parameter used as an object (attribute access or method call)
# must be typed as the universal tagged-union value, like an unannotated
# regular-function parameter, so reads and virtual dispatch resolve on the
# argument's runtime class -- not collapse to a nondet int (the old lambda
# default). Covers the common key/predicate idioms over objects, including
# as arguments to the builtin higher-order functions filter() and map().
class P:
    def __init__(self, age: int) -> None:
        self.age = age

    def label(self) -> int:
        return self.age * 2


def t() -> None:
    key = lambda p: p.age
    meth = lambda p: p.label()
    a = P(30)
    b = P(20)
    assert key(a) == 30
    assert meth(a) == 60
    assert key(b) == 20

    xs = [P(30), P(20)]
    evens = list(filter(lambda p: p.age > 25, xs))
    assert len(evens) == 1
    ages = list(map(lambda p: p.age, xs))
    assert ages[0] == 30


t()
