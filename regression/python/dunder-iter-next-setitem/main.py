# PLR §3.3.1 data model — custom iterator protocol
# via __iter__ / __next__. Used by 'for x in obj'.

class Range3:
    def __init__(self) -> None:
        self.i = 0

    def __iter__(self):
        self.i = 0
        return self

    def __next__(self) -> int:
        if self.i >= 3:
            raise StopIteration
        v = self.i
        self.i = self.i + 1
        return v


# PLR §3.3.1 data model — custom __setitem__.
class Store:
    def __init__(self) -> None:
        self.last_key = 0
        self.last_val = 0

    def __setitem__(self, k: int, v: int) -> None:
        self.last_key = k
        self.last_val = v


total = 0
for x in Range3():
    total = total + x
assert total == 3  # 0 + 1 + 2

s = Store()
s[42] = 99
assert s.last_key == 42
assert s.last_val == 99
