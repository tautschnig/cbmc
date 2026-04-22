def greet(name: str, greeting: str) -> int:
    return len(name) + len(greeting)

x: int = greet(greeting="hi", name="world")
assert x == 7
