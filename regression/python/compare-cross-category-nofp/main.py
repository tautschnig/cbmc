# PLR §6.10.1 no-false-positive guard: same-category orderings are valid and
# must NOT be flagged by the cross-category check -- numeric tower (int<float),
# str<str, list<list (lexicographic), chained comparison, and an Any/param
# operand (category 0). (bool<int is valid too but its RESULT precision is a
# separate pre-existing gap, so not asserted here.)
assert (1 < 2.5) == True
assert ("a" < "b") == True
assert ([1] < [2]) == True
assert (1 < 2 < 3)


def f(x) -> bool:
    return x < 5


assert f(1) == True
