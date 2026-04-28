# PLR §4.2: variable from enclosing scope not found in function
x: int = 10
def foo() -> int:
    return x + 1  # x should be visible from module scope
assert foo() == 11
