# PLR §8.6: structural pattern matching + string comparison
# through branches. The string_constants tracking must be
# invalidated inside branches so the comparison falls through
# to the string solver (path-sensitive via SSA).

def match_test() -> None:
    val: int = 2
    match val:
        case 1:
            r = "one"
        case 2:
            r = "two"
        case _:
            r = "other"
    assert r == "two"


def if_else_test() -> None:
    x: int = 2
    if x == 2:
        r = "two"
    else:
        r = "other"
    assert r == "two"


match_test()
if_else_test()
