# PLR §8.7: typing.overload decorator
class Foo:
    def __init__(self) -> None:
        self.x: int = 0

    def method(self, val: int) -> int:
        self.x = val
        return self.x

f = Foo()
f.method(5)
assert f.x == 5
