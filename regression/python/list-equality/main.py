# PLR §6.10.1 + §6.2.9: list equality across element-type
# boundaries.
#
# Two architectural fixes:
# 1. List equality with mismatched element types now does an
#    element-wise content comparison up to length, rather than
#    falling through to struct-equality (which crashes on
#    incompatible widths) or unsoundly returning never-equal.
# 2. Generator functions zero the data buffer at function
#    entry so the trailing zeros match a list literal's
#    trailing zeros — required for struct-equality to hold
#    even when both sides have the same element type.

def gen():
    yield 1
    yield 2
    yield 3


def gen_to_list_eq() -> None:
    xs = list(gen())
    assert xs == [1, 2, 3]


def gen_eq_literal() -> None:
    xs = gen()
    assert xs == [1, 2, 3]


def mixed_int_float() -> None:
    # PLR §6.10.1: 1 == 1.0 in Python, so [1, 2] == [1.0, 2.0]
    xs = [1, 2, 3]
    ys = [1.0, 2.0, 3.0]
    assert xs == ys


def nested_vs_flat() -> None:
    # Element types are structurally incompatible — never equal
    assert not ([[1]] == [1])


gen_to_list_eq()
gen_eq_literal()
mixed_int_float()
nested_vs_flat()
