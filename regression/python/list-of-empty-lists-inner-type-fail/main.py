def fill() -> int:
    slots = [[] for _ in range(3)]
    slots[0].append("a")
    slots[1].append(7)
    return len(slots[0]) + len(slots[1])


assert fill() == 2
q = [[], []]
q[0].append("x")
q[1].append(5)
assert isinstance(q[0][0], str)
assert isinstance(q[1][0], int)
assert q[1][0] == 6
