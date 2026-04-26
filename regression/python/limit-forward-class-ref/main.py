# PLR §4.7.2: forward references in annotations
# Note: forward references require the class to be defined before use
# in the current implementation (pass ordering limitation).
class Foo:
    def __init__(self, x: int) -> None:
        self.x: int = x

def make_foo() -> "Foo":
    return Foo(1)

f: Foo = make_foo()
assert f.x == 1
