# differential2 §10 (bound-method reassignment). A name bound to a
# bound method in one branch and a different one in another
# (`if cond: m = a.get else: m = b.get`) must dispatch path-sensitively:
# the runtime callable tag set per branch selects both the method and
# its receiver, so each branch's m() resolves to the right object.
class C:
    def __init__(self, v: int) -> None:
        self.v = v

    def get(self) -> int:
        return self.v


def pick(cond: bool) -> int:
    a = C(10)
    b = C(20)
    if cond:
        m = a.get
    else:
        m = b.get
    return m()


assert pick(True) == 10
assert pick(False) == 20
