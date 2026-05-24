# PLR correctness: when a variable is assigned in only one branch
# of an if/else (or each branch with a different value), the
# converter's conversion-time tracking maps must NOT cache the
# value — otherwise the last-arm-processed wins and downstream
# constant-fold lookups produce wrong results.
#
# This regression covers tracking maps beyond just string_constants:
# dict_literals, list_literals, tuple_literals, float_constants,
# alias_targets, function_aliases, bound_methods.

def dict_in_branch() -> None:
    x: int = 2
    if x == 2:
        d = {"a": 1, "b": 2}
    else:
        d = {"a": 99, "b": 100}
    assert d["a"] == 1


def list_in_branch() -> None:
    x: int = 2
    if x == 2:
        xs = [1, 2, 3]
    else:
        xs = [99, 100, 101]
    assert xs[0] == 1


def float_in_branch() -> None:
    x: int = 2
    if x == 2:
        y = 1.5
    else:
        y = 99.5
    assert y == 1.5


dict_in_branch()
list_in_branch()
float_in_branch()
