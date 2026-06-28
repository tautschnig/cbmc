class Victim:
    def __init__(self) -> None:
        self.x: "int | str" = 1


class CM:
    def __init__(self, t: Victim) -> None:
        self.t = t

    def __enter__(self) -> "CM":
        self.t.x = "s"
        return self

    def __exit__(self, a: object, b: object, c: object) -> bool:
        return False


v = Victim()
with CM(v):
    pass
r = v.x - 1
