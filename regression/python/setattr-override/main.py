class C:
    def __init__(self) -> None:
        self.x: int = 5

    def __setattr__(self, n: str, v: object) -> None:
        object.__setattr__(self, n, "s")


c = C()
r = c.x - 1
