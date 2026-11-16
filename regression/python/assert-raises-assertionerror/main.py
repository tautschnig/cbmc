def gate(v: int) -> None:
    assert v != 3


def accepted(v: int) -> bool:
    try:
        gate(v)
    except AssertionError:
        return False
    return True


assert accepted(1)
assert not accepted(3)
