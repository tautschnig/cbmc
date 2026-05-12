# PLR §3.3.1 data model — custom __getitem__ dunder.
# 'obj[key]' dispatches to obj.__getitem__(key) when
# the class defines one.

class Array:
    def __init__(self) -> None:
        pass

    def __getitem__(self, i: int) -> int:
        return i * 10


a = Array()
assert a[5] == 50
assert a[3] == 30
assert a[0] == 0
