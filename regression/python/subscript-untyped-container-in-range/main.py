# In-range, negative and caught out-of-range subscripts of containers read
# from an untyped parameter.
def pick(t, j):
    try:
        return t['k'][j]
    except IndexError:
        return -1


def look(t, key):
    try:
        return t['m'][key]
    except KeyError:
        return -2


cols = {'k': [4, 5], 'm': {'a': 1}}
assert pick(cols, 1) == 5
assert pick(cols, -2) == 4
assert pick(cols, 2) == -1
assert look(cols, 'a') == 1
assert look(cols, 'b') == -2
