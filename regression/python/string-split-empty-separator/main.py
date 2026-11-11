# split, rsplit, partition and rpartition raise ValueError for an empty
# separator, constant or symbolic.
s = nondet_str()
t = nondet_str()
if t == "":
    try:
        p = s.split(t)
        assert False
    except ValueError:
        pass
    try:
        p = s.partition(t)
        assert False
    except ValueError:
        pass
try:
    p = s.rsplit("")
    assert False
except ValueError:
    pass
try:
    p = s.rpartition("")
    assert False
except ValueError:
    pass
