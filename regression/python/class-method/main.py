class Counter:
    def __init__(self, start: int) -> None:
        self.value = start

    def increment(self) -> None:
        self.value = self.value + 1

    def get(self) -> int:
        return self.value

c = Counter(0)
c.increment()
c.increment()
c.increment()
assert c.get() == 3
