# A constant empty `old` with a symbolic `new` takes the same route.
s = nondet_str()
t = nondet_str()
assert s.replace("", t) == s
