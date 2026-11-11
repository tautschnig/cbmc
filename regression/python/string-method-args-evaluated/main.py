# The arguments of a string method are evaluated even when the method itself
# is modelled by a nondet result: here the argument raises ValueError.
s = nondet_str()
u = nondet_str()
if s == "" and u == ".1":
    try:
        r = u.strip(u.split(s)[0])
        assert False
    except ValueError:
        pass
