# s may end with "x": this assertion fails.
s = nondet_str()
assert s.endswith("x") == False
