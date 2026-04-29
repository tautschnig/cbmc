def outer() -> int:
    x: int = 10
    def middle() -> int:
        def inner() -> int:
            return x
        return inner()
    return middle()

assert outer() == 10
