class Span:
    depth: int

    def __init__(self) -> None:
        self.depth = 0

    def __enter__(self) -> "Span":
        self.depth = self.depth + 1
        return self

    def __exit__(self, a, b, c) -> bool:
        self.depth = self.depth - 1
        return False


span = Span()
with span:
    assert span.depth == 2
