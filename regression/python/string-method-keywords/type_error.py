# A keyword that a string method does not take raises TypeError.
s = nondet_str()
try:
    s.find("a", start=1)
    assert False
except TypeError:
    pass
try:
    "ab".split(",", sep=",")
    assert False
except TypeError:
    pass
