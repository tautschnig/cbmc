# PLR §4.4.4: sorted(iterable, key=lambda o: o.attr) must order objects by
# the attribute key (the elements are class structs; comparing them
# directly is meaningless). Both ascending and reverse are supported.
class P:
    def __init__(self, age: int) -> None:
        self.age = age


def t() -> None:
    xs = [P(30), P(20), P(25)]
    ys = sorted(xs, key=lambda p: p.age)
    assert ys[0].age == 20
    assert ys[1].age == 25
    assert ys[2].age == 30

    zs = sorted(xs, key=lambda p: p.age, reverse=True)
    assert zs[0].age == 30
    assert zs[2].age == 20

    # plain int sort still works
    a = sorted([3, 1, 2])
    assert a[0] == 1 and a[2] == 3


t()
