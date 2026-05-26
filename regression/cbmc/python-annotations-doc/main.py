# PLR §3.1, §3.2: annotations are documentation, not enforcement.
# A typed declaration does NOT coerce the bound value at runtime.

# Scalar variant: the annotation says int, but the function returns
# str. Python binds x to the string. The comparison succeeds.
def greet() -> str:
    return "Hi"

x: int = greet()
assert x == "Hi"

# Same shape but with a class method.
class Dog:
    def bark(self) -> str:
        return "Woof!"

def test():
    d = Dog()
    sound: int = d.bark()
    assert sound == "Woof!"

test()

# Annotated literal mismatch — the annotation is purely
# documentation.
y: float = "hello"
assert y == "hello"
