# The first exception raised wins: a later IndexError in the same
# expression must not replace an earlier ValueError.
s = nondet_str()
t = nondet_str()
if t == "" and s == "ab":
    try:
        c = s.split(t)[5]
        assert False
    except ValueError:
        pass
xs = [1, 2]
try:
    c = s[xs.index(3)]
    assert False
except ValueError:
    pass
