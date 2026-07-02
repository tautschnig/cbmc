# PLR §6.2.7: dict keys coalesce by Python equality. Tuple keys are equal when
# element-wise equal, incl. cross-type numeric (1 == 1.0), so {(1,2),(1,2.0)}
# is ONE key. Unified canonical_key recurses into tuples. Real len is 1.
assert len({(1, 2): "a", (1, 2.0): "b"}) == 1
assert len({(1, (2, 3)): "x", (1, (2, 3.0)): "y"}) == 1  # nested
assert len({(1, 2): "a", (1, 3): "b"}) == 2  # genuinely distinct
