# The exception type is set: ZeroDivisionError for -0.0 and for //= 0,
# ValueError for a negative shift.
x = 1.0
y = -0.0
try:
    x / y
    assert False
except ZeroDivisionError:
    pass
try:
    1 << -1
    assert False
except ValueError:
    pass
w = 4
try:
    w //= 0
    assert False
except ZeroDivisionError:
    pass
