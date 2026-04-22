MAX_SIZE = 100

def check(x: int) -> None:
    __CPROVER_assume(x >= 0 and x < MAX_SIZE)
    assert x < MAX_SIZE
    assert x >= 0
