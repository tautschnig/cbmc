from typing import Any

class Foo:
    def __init__(self) -> None:
        pass
    def foo(self, x: int) -> int:
        return x + 1

f = Foo()
assert f.foo(5) == 6
