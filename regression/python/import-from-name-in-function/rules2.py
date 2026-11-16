def gate(v: int) -> None:
    if v % 2 != 0:
        raise AssertionError()
