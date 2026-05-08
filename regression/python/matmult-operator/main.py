def mat_mult(a: int, b: int) -> int:
    # PLR §6.7: Python's '@' binary operator. For scalar ints it has
    # no built-in meaning, so our front-end models it as a scalar
    # product — the observable behaviour here matches '*'.
    return a @ b


def aug_mat_mult(x: int) -> int:
    y: int = x
    y @= 5
    return y


assert mat_mult(3, 4) == 12
assert aug_mat_mult(6) == 30
