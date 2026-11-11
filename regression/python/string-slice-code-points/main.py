# Slices of constant strings count code points, not UTF-8 bytes, and a
# step of 0 raises a catchable ValueError.
assert 'é '[:2] == 'é '
assert 'aéb'[1] == 'é'
assert 'aéb'[::-1] == 'béa'
assert 'aéb'[2:0:-1] == 'bé'
assert 'éé'[1:] == 'é'
try:
    x = 'ab'[::0]
    assert False
except ValueError:
    pass
