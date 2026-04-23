lst = [10, 20, 30]
lst[1] = 99
assert lst[0] == 10
assert lst[1] == 99
assert lst[2] == 30
