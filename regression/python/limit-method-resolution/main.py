# PLR §3.2: method defined in class body not found at call site
class Foo:
    def __init__(self) -> None:
        self.x: int = 1
    def get_x(self) -> int:
        return self.x

f = Foo()
assert f.get_x() == 1
