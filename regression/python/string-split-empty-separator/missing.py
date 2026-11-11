# s.split(t) does not raise for a non-empty t: this assertion fails.
s = nondet_str()
t = nondet_str()
if t == "a":
    try:
        p = s.split(t)
    except ValueError:
        assert False
