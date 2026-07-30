# Vacuity guard: the widened field still refutes wrong values, and
# --python-check-annotations (run in the sibling desc via the positive
# test) is unaffected.
class R:
    kind: str

    def __init__(self, kind: str) -> None:
        self.kind = kind


def f():
    r = R(1)
    assert r.kind == 2


f()
