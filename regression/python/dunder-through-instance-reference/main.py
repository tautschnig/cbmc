class Bag:
    def __init__(self, n: int) -> None:
        self.n = n

    def __len__(self) -> int:
        return self.n

    def __getitem__(self, i: int) -> int:
        return i * 10

    def __contains__(self, v: int) -> bool:
        return v < self.n

    def __bool__(self) -> bool:
        return self.n > 1

    def __eq__(self, other: object) -> bool:
        return True

    def __lt__(self, other: "Bag") -> bool:
        return self.n < other.n

    def __add__(self, other: "Bag") -> int:
        return self.n + other.n


def probe(b: Bag, c: Bag) -> None:
    assert len(b) == 3
    assert b[2] == 20
    assert 2 in b
    assert b
    assert b == c
    assert c < b
    assert b + c == 5


probe(Bag(3), Bag(2))
