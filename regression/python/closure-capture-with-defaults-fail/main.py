# Twin: m(1) is 103, not 101.
def outer() -> int:
    k = 100
    def m(a: int, b: int = 2) -> int:
        return a + b + k
    return m(1)


assert outer() == 101
