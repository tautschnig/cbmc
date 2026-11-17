# Each assertion fails in CPython. A set compared as a multiset with a list
# (`{"a"} == ["a"]` verified), and a set of objects with __eq__ and __hash__
# (a dict) raised AttributeError from add, so a handler made the assertion
# unreachable.
__assert_k = nondet_int(); assert __assert_k != 0 or ({"a"} == ["a"]), "set and list"


class P:
    def __init__(self, v: int):
        self.v = v

    def __eq__(self, o) -> bool:
        return self.v == o.v

    def __hash__(self) -> int:
        return self.v


def objects():
    s = {P(1), P(2)}
    try:
        s.add(P(3))
        assert __assert_k != 1 or (False), "add"
    except AttributeError:
        pass


objects()
