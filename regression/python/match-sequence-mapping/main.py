# PLR 10.6.3: MatchSequence — list destructuring via match.
# Works for annotated list parameters; untyped parameters
# and dict patterns have limitations.


def classify(lst: list) -> int:
    match lst:
        case []:
            return 0
        case [x]:
            return x
        case [x, y]:
            return x + y
        case [a, *rest]:
            return a


assert classify([]) == 0
assert classify([42]) == 42
assert classify([3, 4]) == 7
assert classify([10, 20, 30, 40]) == 10
