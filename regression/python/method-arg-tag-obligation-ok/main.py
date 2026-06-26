# The method-call tag obligation must NOT fire when the Any value's runtime tag
# matches the annotated scalar param (int here).
from typing import Any


class Foo:
    def bar(self, n: int) -> int:
        return n + 1


def f(x: Any) -> int:
    obj = Foo()
    return obj.bar(x)


assert f(5) == 6
