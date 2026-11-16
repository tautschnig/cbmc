from rules import gate


def ok(v: int) -> bool:
    try:
        gate(v)
    except AssertionError:
        return False
    return True


assert ok(4)
assert not ok(5)
