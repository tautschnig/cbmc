l: list[str] = ["hello", "world"]
total: int = 0
for s in l:
    total = total + len(s)
assert total != 10
