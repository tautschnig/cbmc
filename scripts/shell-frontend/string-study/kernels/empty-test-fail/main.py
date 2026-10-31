x = nondet_str()
y = "default" if len(x) == 0 else x
assert y != "x"
