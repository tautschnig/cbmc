class A:
    def __init__(self, v: int) -> None:
        self.n = 0
        self._add(v)

    def _add(self, v: int) -> None:
        self.n = self.n + v
