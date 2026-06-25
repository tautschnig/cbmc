# Soundness: `del obj.attr` on an instance-only attribute must not leave a
# concrete stale/zero value that a later read can be proved against. CPython
# raises AttributeError after del; we cannot flow-sensitively model that for
# instance-only attrs (no presence flag), so the slot is havocked to nondet --
# a read can no longer be proved equal to any concrete value. Both assertions
# below are therefore NOT provable (the old code proved `== 0`).
class C:
    def __init__(self) -> None:
        self.x: int = 42

    def clear(self) -> None:
        del self.x

c = C()
c.clear()
assert c.x == 0
