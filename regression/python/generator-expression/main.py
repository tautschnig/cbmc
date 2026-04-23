result = all(x > 0 for x in [1, 2, 3, 4, 5])
assert result

result2 = all(x > 3 for x in [1, 2, 3, 4, 5])
assert not result2

result3 = any(x > 4 for x in [1, 2, 3, 4, 5])
assert result3
