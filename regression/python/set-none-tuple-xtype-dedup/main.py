# PLR §6.2.6: set elements coalesce by Python equality. None is a singleton
# ({None, None} -> 1); tuple elements coalesce element-wise incl. cross-type
# numeric. Same unified canonical_key as the dict builder.
assert len({None, None}) == 1
assert len({(1, 2), (1, 2.0)}) == 1
assert len({1, 1.0}) == 1
assert len({1, "a", None}) == 3  # genuinely distinct
