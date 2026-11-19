# format() of a symbolic int with native SMT strings.
def nondet_int() -> int: ...


v = nondet_int()
__CPROVER_assume(0 <= v and v < 16)
assert len(format(v, "x")) == 1
assert format(-26, "#06x") == "-0x01a" and format("abc", ">5.2") == "   ab"
