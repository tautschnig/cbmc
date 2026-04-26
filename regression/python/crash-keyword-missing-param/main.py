# PLR §6.3.4: keyword arguments with missing positional
def foo(w: int, x: int, y: int = 3) -> int:
    return x + y

result: int = foo(w=3)
assert result == 3
