# Appending objects to an initially-empty list must preserve each
# element's runtime class so a later x[i].method() dispatches virtually
# -- the empty list's default scalar element type used to drop the class
# tag. Covers constructor appends, mixed subclasses, and appending an
# element subscripted from another list (incl. through a higher-order
# function specialised at the call site).
class Base:
    def m(self) -> int:
        return 0


class A(Base):
    def m(self) -> int:
        return 1


class B(Base):
    def m(self) -> int:
        return 2


def keep(xs, pred):
    out = []
    i = 0
    while i < len(xs):
        if pred(xs[i]):
            out.append(xs[i])
        i += 1
    return out


def t() -> None:
    # constructor appends, mixed subclasses
    r = []
    r.append(A())
    r.append(B())
    assert r[0].m() == 1
    assert r[1].m() == 2

    # append a subscripted element of a literal object list
    src = [A(), B()]
    s = []
    j = 0
    while j < len(src):
        s.append(src[j])
        j += 1
    assert s[0].m() == 1
    assert s[1].m() == 2

    # through a higher-order function over a list parameter
    kept = keep([A(), B()], lambda e: True)
    assert kept[0].m() == 1
    assert kept[1].m() == 2


t()
