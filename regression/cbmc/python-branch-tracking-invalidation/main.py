# PLR §6.10.1: when a string variable is assigned in only one
# branch of an if/else (or each branch with a different value),
# the converter's string_constants tracking map is path-
# insensitive — the last write wins, even though only one arm
# executes at runtime. This causes assertions like
# `assert r == "two"` to fold to `false` at conversion time.
#
# The fix gates string_constants writes by if_else_depth: when
# inside a branch, erase the entry instead of writing it, so the
# string equality falls through to the string solver (which
# handles path-sensitivity correctly via SSA).
#
# This test exercises the string-content path. Structural
# tracking (dict_literals, list_literals, etc.) is intentionally
# kept path-insensitive: invalidating it inside a branch breaks
# downstream constant-folding of in-same-branch reads (e.g. a
# list comp reading a list literal assigned one statement back),
# which is a more common pattern than the path-insensitive
# folding bug at the join.

def string_in_branch() -> None:
    x: int = 2
    if x == 2:
        r = "two"
    else:
        r = "other"
    assert r == "two"


def string_in_match() -> None:
    val: int = 2
    match val:
        case 1:
            r = "one"
        case 2:
            r = "two"
        case _:
            r = "other"
    assert r == "two"


string_in_branch()
string_in_match()
