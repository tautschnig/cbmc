# Refined Python strings on an SMT back end: the characters of a nondet
# string are not reachable in the SMT encoding, so its string queries are
# unknown. Each assertion below fails for some s.
s = nondet_str()
t = nondet_str()
assert not ("ab" in s)
