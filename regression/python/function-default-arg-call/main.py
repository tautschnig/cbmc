# A function passed as a default argument value and called through the
# parameter resolves to the function (per-call-site monomorphisation).
def f(x: int, y: int) -> int:
    return x * y


g = f


def h(op=g) -> int:
    return op(1, 1)


assert h() == 1
