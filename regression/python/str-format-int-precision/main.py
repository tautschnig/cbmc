# str(n) and "{}".format(n) for int n now emit
# cprover_string_of_int_func, giving the solver precise
# content/length.


def stringify(n: int) -> str:
    return str(n)


def formatted(n: int) -> str:
    return "{}".format(n)


def zero_arg(n: int) -> str:
    return "{0}".format(n)


# Precise lengths — the solver knows.
assert len(stringify(42)) == 2
assert len(stringify(-5)) == 2  # "-5"
assert len(formatted(1000)) == 4
assert len(zero_arg(7)) == 1
