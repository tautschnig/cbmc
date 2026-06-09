# A user-defined higher-order function that runs a comprehension over its
# (unannotated) list parameter, filtering with the function-valued
# parameter, must work after monomorphisation: the clone is specialised to
# the call-site argument types (so `xs` is recognised as a list, not a
# generic value) and re-infers its return type from the specialised body
# (so `len(result)` doesn't spuriously raise TypeError).
def keep(xs, pred):
    return [x for x in xs if pred(x)]


class P:
    def __init__(self, v: int) -> None:
        self.v = v


def t() -> None:
    # ints
    e = keep([1, 2, 3], lambda x: x > 1)
    assert len(e) == 2
    # objects: predicate dispatches on the element's attribute
    ps = keep([P(1), P(9)], lambda p: p.v > 5)
    assert len(ps) == 1
    # same HOF, different predicate -> distinct sound clone
    f = keep([1, 2, 3, 4], lambda x: x > 2)
    assert len(f) == 2


t()
