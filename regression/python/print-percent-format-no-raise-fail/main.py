# Printing a %-formatted value next to a list does not raise.
def step(level: int, marks: list[int]) -> int:
    print("level %d" % (level), marks)
    print("pair %d:%d" % (level, level * 2))
    return level - 1


assert step(5, [9, 8]) == 5
