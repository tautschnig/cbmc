class Box:
    def __init__(self, v: int) -> None:
        self.v = v

    def __len__(self) -> int:
        return self.v


def size(x) -> int:
    return len(x)


def make():
    return [1, 2]


assert size(Box(3)) == 3
assert size(make()) == 2      # second call site: a list, not a Box
