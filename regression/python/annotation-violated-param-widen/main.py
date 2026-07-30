# PLR §3.1: "no type checking happens at runtime" — an annotation never
# constrains the value actually passed. R(1) with `kind: str` stores
# int 1; reading it back compares as an int. Previously the annotated
# param/field punned the int into the string representation: a false
# alarm on the default backend, a member_exprt invariant abort on the
# native SMT-strings backend (the parameter member of the slot-pun /
# Any-dominance family: dict values, list elements and self.x: T inits
# already widened).
class R:
    kind: str

    def __init__(self, kind: str) -> None:
        self.kind = kind


def violated():
    r = R(1)
    assert r.kind == 1


def conforming():
    # Conforming calls keep string precision.
    r = R("a")
    assert r.kind == "a"


violated()
conforming()
