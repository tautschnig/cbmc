# PLR 10.6: match statement patterns.
#
# Supported in this test:
#   - MatchValue   — constant pattern (case 1, case "x")
#   - MatchSingleton — None / True / False
#   - MatchOr      — case A | B | C
#   - MatchAs      — binding name, optional inner pattern
#   - wildcard _
#   - guards       — case P if cond:


# Basic value patterns.
def classify(x: int) -> int:
    match x:
        case 1:
            return 10
        case 2:
            return 20
        case _:
            return 99


assert classify(1) == 10
assert classify(2) == 20
assert classify(5) == 99


# Or-patterns.
def is_small(x: int) -> int:
    match x:
        case 1 | 2 | 3:
            return 1
        case _:
            return 0


assert is_small(1) == 1
assert is_small(3) == 1
assert is_small(4) == 0


# Name binding (wildcard-binding).
def get_val(x: int) -> int:
    match x:
        case 0:
            return -1
        case v:
            return v


assert get_val(0) == -1
assert get_val(5) == 5
assert get_val(-7) == -7


# Guard.
def guarded(x: int) -> int:
    match x:
        case n if n > 10:
            return 1
        case n if n > 0:
            return 2
        case _:
            return 3


assert guarded(100) == 1
assert guarded(5) == 2
assert guarded(-1) == 3
assert guarded(0) == 3


# Singleton: None.
def is_none(x) -> int:
    match x:
        case None:
            return 1
        case _:
            return 0


assert is_none(None) == 1
assert is_none(5) == 0
