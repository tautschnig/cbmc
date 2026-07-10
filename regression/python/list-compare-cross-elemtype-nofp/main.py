# Cross-element-type list ==/!= is precise in both directions; None vs list and
# genuine differences unaffected.
assert [6, 0] == [6, False]
assert [6, 1] != [6, False]
assert not ([6, 1] == [6, False])
x = None
assert x != []
assert not (x == [])
