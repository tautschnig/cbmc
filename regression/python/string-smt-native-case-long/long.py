# Executions where the subject of upper() is longer than
# --python-max-string-length must not be ruled out: t has 6 characters when s
# has 2, so this assertion fails.
s = nondet_string(2)
t = s + s + s
u = t.upper()
assert len(t) != 6
