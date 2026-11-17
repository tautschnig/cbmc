# A function reading globals that are assigned after it in the module.
def depth() -> int:
    return len(stacks[1]) + LIMIT


stacks = [[] for k in range(3)]
LIMIT = 2 * 4
stacks[1].append(5)
assert depth() == 8
