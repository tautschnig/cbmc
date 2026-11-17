# format(value, spec) for ints and strs; round trip through int(_, 16).
def nondet_int() -> int: ...


assert format(3054, "08X") == "00000BEE"
assert format(-5, "+06d") == "-00005"
assert format(10, "#b") == "0b1010"
assert format("ab", "*^6") == "**ab**"
assert format(77) == "77"

v = nondet_int()
__CPROVER_assume(v >= 0)
h = format(v, "016x")
assert len(h) == 16
assert int(h, 16) == v
