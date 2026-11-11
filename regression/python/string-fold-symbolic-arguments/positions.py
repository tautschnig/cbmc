# The arguments of startswith/endswith with positions are evaluated: here
# one raises ValueError.
u = nondet_str()
k = nondet_int()
if k == -1 and u == "a":
    try:
        b = u.endswith("a".split("")[k], 3, k)
        assert False
    except ValueError:
        pass
