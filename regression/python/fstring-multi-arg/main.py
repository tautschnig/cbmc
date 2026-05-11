# Multi-part f-string precision (PLR 2.4.3).
#
# Each part — literal segment, int FormattedValue, float
# FormattedValue, str FormattedValue — becomes a string,
# then all parts chain-concatenate via
# cprover_string_concat_func.


def pair_str(a: int, b: int) -> str:
    return f"{a}:{b}"


s = pair_str(7, 42)
assert len(s) == 4  # "7:42"


def labeled(tag: str, n: int) -> str:
    return f"{tag}={n}"


t = labeled("x", 100)
assert len(t) == 5  # "x=100"


def multi(a: int, b: int, c: int) -> str:
    return f"{a},{b},{c}"


r = multi(1, 22, 333)
assert len(r) == 8  # "1,22,333"


# Single-part case still uses the direct of_int path.
u = pair_str(5, 5)
assert len(u) == 3  # "5:5"
