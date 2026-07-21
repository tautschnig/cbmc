# --python-unbounded-ints INT-ID HANDLES (inttab : bv64 -> Int, the
# strtab pattern for mathematical integers): the integer* box
# byte-extracted the pointed variable-width integer when value sets
# failed to resolve (probe-confirmed same unpack_struct family as string
# pointer boxing). Unlike the pv __str payload (regex intrinsics recover
# string CONSTANTS syntactically -- UF-incompatible), integer payloads
# feed ARITHMETIC, which accepts any Int term. Exact big-int round-trips
# through the Any channel, class fields, and list elements.
from typing import Any


class C:
    n: int

    def __init__(self) -> None:
        self.n = 10**30


def f() -> Any:
    return 10**30


x: Any = f()
assert x == 10**30
c = C()
assert c.n == 10**30
assert c.n + 1 == 10**30 + 1
small: Any = 5
assert small == 5
