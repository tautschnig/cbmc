# PLR §8.3: for-else — the else clause runs when the
# loop exhausts the iterable without hitting break.
# PLR §8.2: while-else — analogous for while loops.

def find_or_not_found():
    for i in range(3):
        if i == 5:
            return "found"
    else:
        return "not-found"
    return "impossible"


def break_skips_else():
    for i in range(3):
        if i == 1:
            break
    else:
        return "complete"
    return "broke"


def while_else_completes():
    i = 0
    while i < 3:
        i = i + 1
    else:
        return "completed"
    return "broke"


def while_else_break():
    i = 0
    while i < 3:
        i = i + 1
        if i == 2:
            break
    else:
        return "completed"
    return "broke"


assert find_or_not_found() == "not-found"
assert break_skips_else() == "broke"
assert while_else_completes() == "completed"
assert while_else_break() == "broke"
