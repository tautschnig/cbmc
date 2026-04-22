counts = [0, 3, 5]
counts[0] += 1
counts[1] -= 1
counts[2] *= 2
assert counts[0] == 1
assert counts[1] == 2
assert counts[2] == 10
