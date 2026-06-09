# Container-stored callables: a call whose callee is a subscript of a dict
# literal of callables selects the matching entry and calls it. A key that
# matches no entry is a KeyError.
def add(a: int, b: int) -> int:
    return a + b


def sub(a: int, b: int) -> int:
    return a - b


def f(op: str) -> int:
    return {"+": add, "-": sub}[op](5, 3)


assert f("+") == 8
assert f("-") == 2

# Lambda values, no args.
def g(x: str) -> float:
    return {"+": lambda: 1.0}[x]()


assert g("+") == 1.0
