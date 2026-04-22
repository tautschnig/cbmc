class Counter:
    def __init__(self, start: int) -> None:
        self.value = start

    def add(self, n: int) -> None:
        self.value += n

c = Counter(10)
c.add(5)
assert c.value == 15
