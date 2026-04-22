def safe_check(x: int) -> int:
    __CPROVER_assume(x >= 0)
    if x < 0:
        raise ValueError("must be non-negative")
    return x
