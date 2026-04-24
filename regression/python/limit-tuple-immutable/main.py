# Python Language Reference §4.6.6: tuples are immutable
# Assignment to tuple element should raise TypeError
t = (1, 2, 3)
try:
    t[0] = 100
    assert False
except TypeError:
    pass
assert t[0] == 1
