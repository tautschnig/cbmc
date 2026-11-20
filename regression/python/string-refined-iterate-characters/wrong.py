# The assertions fail in CPython.
s = nondet_string(2)
n = 0
for c in s:
    n += 1
    if n == 2:
        assert c != "€"
