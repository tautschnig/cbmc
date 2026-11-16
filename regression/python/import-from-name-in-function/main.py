from rules2 import gate


def ok(v: int) -> bool:
    gate(v)
    return True


assert ok(4)
