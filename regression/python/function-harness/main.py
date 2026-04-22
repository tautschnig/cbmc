def double_it(x: int) -> int:
    return x * 2

def test_double(x: int) -> None:
    __CPROVER_assume(x > 0 and x < 100)
    result: int = double_it(x)
    assert result == x * 2
    assert result > 0
