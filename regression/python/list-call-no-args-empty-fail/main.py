# list() with no argument is a new empty list.
acc = list()
assert len(acc) == 0
acc.append(3)
assert acc == [3, 3]
