x: int = 0

# Simplified: we just test that the with body is executed.
# The context manager protocol (__enter__/__exit__) is not yet modeled.
# This test uses a bare 'with' without 'as'.

class Mgr:
    def __init__(self) -> None:
        pass
    def __enter__(self) -> int:
        return 0
    def __exit__(self, a: int, b: int, c: int) -> None:
        pass

with Mgr():
    x = 42
assert x == 42
