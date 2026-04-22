class Animal:
    def __init__(self, name: str) -> None:
        self.name = name

    def legs(self) -> int:
        return 0

class Dog(Animal):
    def legs(self) -> int:
        return 4

d = Dog("Rex")
assert d.legs() == 4
