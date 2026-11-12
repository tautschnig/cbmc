# int("1  ") is 1: the solver's Long.parseLong made it some other number,
# which verified this assertion.
t = nondet_str()
if t == "1  ":
    assert int(t) != 1
