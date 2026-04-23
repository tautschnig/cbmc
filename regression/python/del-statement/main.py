lst = [1, 2, 3]
del lst[1]
assert len(lst) == 2
assert lst[0] == 1
assert lst[1] == 3
