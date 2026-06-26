# Precision: an int-keyed dict subscript re-store must be visible to a
# subsequent read. The dict_literals const-fold key-array is string-keyed, so an
# int/bool key store used to leave a STALE construction value (`d={1:10};
# d[1]=20; d[1]` folded to 10). Now the const-fold is dropped for the dict on
# such a store, so the read falls back to the (updated) runtime values array.
d = {1: 10, 2: 99}
d[1] = 20
assert d[1] == 20
assert d[2] == 99
d[3] = 30          # new int key
assert d[3] == 30
b = {True: 1}
b[True] = 7
assert b[True] == 7
