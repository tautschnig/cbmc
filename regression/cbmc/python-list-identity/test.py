# Regression: per PLR §6.10.3, list/dict literals create distinct
# heap objects; `[1,2,3] is [1,2,3]` is False even though the lists
# compare equal. Previously the frontend folded `is` to a structural
# equal_exprt, so the assertion silently passed.
x = [1, 2, 3]
y = [1, 2, 3]
assert x is y  # must FAIL
