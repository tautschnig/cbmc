# PLR 6.4.6: set.pop() on an empty set raises KeyError. (Regression: an over-eager
# member-assume was cutting the empty-set path and masking the KeyError.)
s = set()
v = s.pop()
