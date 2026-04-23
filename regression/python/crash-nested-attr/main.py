# Nested attribute access: obj.inner.value
class Inner:
    def __init__(self, v: int) -> None:
        self.value = v

class Outer:
    def __init__(self, v: int) -> None:
        self.inner = Inner(v)

o = Outer(42)
assert o.inner.value == 42
