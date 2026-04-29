def outer(x: int) -> int:
    def inner() -> int:
        return x
    return inner()

assert outer(42) == 42
