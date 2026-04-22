total: int = 0
i: int = 0
while i < 10:
    i = i + 1
    if i == 3:
        continue
    if i == 7:
        break
    total = total + i
# total = 1 + 2 + 4 + 5 + 6 = 18
assert total == 18
