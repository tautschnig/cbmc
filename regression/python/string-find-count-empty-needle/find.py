# ''.find('', -6, -6) is 0, beyond the bound len(s) - 1 of the nondet model.
s = nondet_str()
j = nondet_int()
if s == "" and j == -6:
    assert "".find(s, j, j) != 0
