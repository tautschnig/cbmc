rows = ['00ff', '-1a', '101']
assert int(rows[0], 16) == 255
assert int(rows[1], 16) == -26
assert int(rows[2], 2) == 5
assert int(rows[2]) == 101
