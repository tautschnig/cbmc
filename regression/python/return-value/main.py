def abs_val(x: int) -> int:
    if x < 0:
        return -x
    return x

assert abs_val(5) == 5
assert abs_val(-3) == 3
assert abs_val(0) == 0
