class Animal:
    def __init__(self, name: str) -> None:
        self.name = name

class Dog(Animal):
    def __init__(self, name: str) -> None:
        self.name = name

d = Dog("Rex")
assert isinstance(d, Dog)
assert isinstance(d, Animal)
