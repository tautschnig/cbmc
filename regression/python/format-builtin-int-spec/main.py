# format(value, spec) for ints and strs; round trip through int(_, 16).
def nondet_int() -> int: ...


assert format(3054, "08X") == "00000BEE"
assert format(-5, "+06d") == "-00005"
assert format(10, "#b") == "0b1010"
assert format("ab", "*^6") == "**ab**"
assert format(77) == "77"

code = nondet_int()
__CPROVER_assume(0 <= code < (1 << 36))
word = format(code, "09X")
assert len(word) == 9
assert int(word, 16) == code
assert format(code, "#X").startswith("0X")
