# PLR §3.1 / §4.2.1: class instances are passed by reference. A mutation through
# a concrete-class-typed parameter must be visible to the caller. (Already works
# -- concrete-class params are pointer-typed; this is a regression guard for the
# reference-semantics-for-instances work, which must not break it.)
class V:
    def __init__(self) -> None:
        self.x: int = 1


def f(t: V) -> None:
    t.x = 99


v = V()
f(v)
assert v.x == 99
