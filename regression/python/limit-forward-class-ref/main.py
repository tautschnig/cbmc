# PLR §4.7.2: forward references in annotations
def make_foo() -> "Foo":
    return Foo(1)

class Foo:
    def __init__(self, x: int) -> None:
        self.x: int = x

f: Foo = make_foo()
assert f.x == 1
