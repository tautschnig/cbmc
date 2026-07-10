# bool/int list equality is precise both ways; genuine differences detected.
assert [0] == [False]
assert [True] == [1]
assert [1] != [False]
assert not ([1] == [False])
