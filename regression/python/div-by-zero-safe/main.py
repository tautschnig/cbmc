def safe_divide(a: int, b: int) -> int:
    __CPROVER_assume(b != 0)
    return a // b
