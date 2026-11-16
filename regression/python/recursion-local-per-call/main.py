def keep(n: int) -> int:
    a = n
    if n > 0:
        keep(n - 1)
    return a


assert keep(2) == 2
