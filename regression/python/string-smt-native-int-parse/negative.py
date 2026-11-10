# int("-1") is negative, so this assertion fails.
s = nondet_str()
assert int(s) >= 0 or len(s) == 0
