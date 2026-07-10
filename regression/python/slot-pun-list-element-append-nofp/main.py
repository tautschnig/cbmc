# CORE no-false-alarm companion: correctly-typed append/insert into list[int]
# must still verify (the widening must fire ONLY for a mismatched/uninferable
# store, not for a matching one -- precision is retained).
xs: list[int] = []
xs.append(5)
xs.insert(0, 7)
assert xs[0] == 7
assert sum(xs) == 12
