# Beyond the replacements that are made one by one, the length of the
# result is still known.
s = nondet_str()
k = nondet_int()
assert len(s.replace("a", "b", k)) == len(s)
