# The matching handler still catches; a genuine ValueError is still caught.
xs = [2]
try:
    a = xs.index(xs[4])
except IndexError:
    a = 2
assert a == 2

try:
    b = [1].index(9)
except ValueError:
    b = 7
assert b == 7

try:
    c = 1 // 0
except ZeroDivisionError:
    c = 3
assert c == 3
