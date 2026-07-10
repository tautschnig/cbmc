# CORE no-false-alarm: a correctly-typed subscript-store into list[int] must
# still verify (the widening fires ONLY on a mismatched/uninferable store).
xs: list[int] = [0]
xs[0] = 9
assert xs[0] == 9
assert isinstance(xs[0], int)
