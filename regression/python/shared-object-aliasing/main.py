class Shared:
    def __init__(self) -> None:
        self.x: "int | str" = 42

    def clear(self) -> None:
        self.x = "c"


class V:
    def __init__(self, s: Shared) -> None:
        self.shared: Shared = s


s = Shared()
a = V(s)
b = V(s)
b.shared.clear()
r = a.shared.x - 1
