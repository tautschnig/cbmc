pairs: list = [(1, 2), (3, 4)]
total: int = 0
for a, b in pairs:
    total = total + a + b
assert total == 10
