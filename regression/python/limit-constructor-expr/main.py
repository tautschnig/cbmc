# Limitation: constructor in expression position returns nondet
class Pair:
    def __init__(self, x: int, y: int) -> None:
        self.x = x
        self.y = y

lst = [Pair(1, 2), Pair(3, 4)]
assert lst[0].x == 1
