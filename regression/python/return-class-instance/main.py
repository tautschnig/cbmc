class Foo:
    def __init__(self, x: int) -> None:
        self.x = x

def make_foo(x: int) -> Foo:
    return Foo(x)

f = make_foo(42)
assert f.x == 42
