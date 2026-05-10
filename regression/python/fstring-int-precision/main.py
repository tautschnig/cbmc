# f-string of a single int expression now emits
# cprover_string_of_int_func so the result's content is
# known precisely (length matches the decimal representation).


def stringify(n: int) -> str:
    return f"{n}"


assert len(stringify(42)) == 2
assert len(stringify(7)) == 1
assert len(stringify(1000)) == 4
