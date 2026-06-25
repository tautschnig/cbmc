# A class defining __getattribute__ intercepts EVERY attribute read and may
# return any type. cbmc does not model the override, so it over-approximates
# reads of such instances to a nondet python_value; using the result as int
# routes through the operator tag obligation and the possible TypeError is
# reported. CPython here returns "s" for c.x, so c.x - 1 raises TypeError.
class C:
    def __init__(self) -> None:
        self.x: int = 5

    def __getattribute__(self, n: str) -> object:
        return "s"


c = C()
r = c.x - 1
