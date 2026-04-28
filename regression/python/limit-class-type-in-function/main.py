# PLR §4.7.2: class type annotation inside function
class Foo:
    def __init__(self, x: int) -> None:
        self.x: int = x

def make() -> Foo:
    return Foo(42)

assert make().x == 42
