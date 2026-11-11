# Constant strings with symbolic arguments are not folded as if the
# arguments were absent: each assertion here fails.
s = nondet_str()
k = nondet_int()
if s == "ab" and k == 4:
    assert "ab".zfill(k) == "ab"
