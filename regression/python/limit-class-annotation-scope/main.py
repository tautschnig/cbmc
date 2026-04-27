# PLR §4.7.2: class type in function parameter annotation
class Foo:
    def __init__(self, x: int) -> None:
        self.x: int = x

def get_x(f: Foo) -> int:
    return f.x

result: int = get_x(Foo(42))
assert result == 42
