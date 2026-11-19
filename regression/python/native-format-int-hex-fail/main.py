# Twin: format(171, "02X") is "AB".
def nondet_int() -> int: ...
v = nondet_int()
__CPROVER_assume(0 <= v and v < 256)
assert format(v, "02X") != "AB"
