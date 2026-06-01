# Differential unsoundness witness B (cbmc-py-differential addendum):
# unwrap_value extracts the requested union field with NO tag check, so
# using a str-tagged value as an int silently reads the stale int field
# instead of raising TypeError. With reference semantics the mutation IS
# seen (c.value becomes str), but the int unwrap doesn't assert the tag.
# CPython: x = c.value is "hello" -> assert isinstance(x, int) FAILS.
# cbmc-py currently reports SUCCESSFUL (false negative). KNOWNBUG until
# tagged-union field extraction emits a tag obligation.
class Cell:
    def __init__(self, v: "int | str") -> None:
        self.value: "int | str" = v
    def to_str(self) -> None:
        self.value = "hello"
c = Cell(42)
if isinstance(c.value, int):
    c.to_str()
    x: int = c.value
    assert isinstance(x, int)
