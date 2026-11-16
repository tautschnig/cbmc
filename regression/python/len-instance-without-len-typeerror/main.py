class Plain:
    def __init__(self) -> None:
        self.v = 1


def size(x) -> int:
    return len(x)


def make():
    return [1]


size(make())
size(Plain())          # TypeError: object of type 'Plain' has no len()
