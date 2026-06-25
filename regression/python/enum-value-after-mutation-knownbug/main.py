from enum import Enum


class S(Enum):
    A = 1
    B = "x"


class J:
    def __init__(self) -> None:
        self.s: S = S.A

    def flip(self) -> None:
        self.s = S.B


j = J()
if j.s == S.A:
    j.flip()
    r = j.s.value - 1
