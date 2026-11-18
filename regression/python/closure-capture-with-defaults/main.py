# A nested function with defaults, a keyword-only default and a capture.
def outer() -> int:
    k = 100
    def m(a: int, b: int = 2, *, c: int = 5) -> int:
        return a + b + c + k
    return m(1, c=7) + m(1, 3)


assert outer() == 110 + 109
