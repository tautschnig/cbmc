from rules3 import gate


def ok(v: int) -> bool:
    gate(v)
    other(v)
    return True


assert ok(4)
