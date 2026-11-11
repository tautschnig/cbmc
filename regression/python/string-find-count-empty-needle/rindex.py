# rindex() of a missing substring raises ValueError, also in the nondet model
# of the native back end.
s = nondet_str()
u = nondet_str()
if s == "aAaB" and u == "AA":
    try:
        r = s.rindex(u)
    except ValueError:
        assert False
