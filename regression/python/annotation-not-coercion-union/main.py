# Differential unsoundness witness B (cbmc-py-differential addendum),
# BinOp-union form. A type annotation is documentation, not a runtime
# coercion: `x: int = <tagged-union value>` must keep the value's
# runtime tag, NOT force-unwrap to int by reading __int_val. After
# to_str() makes c.value a str, x retains the str tag, so
# isinstance(x, int) is False and this assertion fails (CPython 3.13).
# Previously cbmc-py force-unwrapped to a concrete int and verified
# SUCCESSFUL (the unsoundness). This is the non-string-annotation
# counterpart of tagged-union-narrowing-unsound.
class Cell:
    def __init__(self, v: int | str) -> None:
        self.value: int | str = v

    def to_str(self) -> None:
        self.value = "hello"


c = Cell(42)
if isinstance(c.value, int):
    c.to_str()
    x: int = c.value
    assert isinstance(x, int)
