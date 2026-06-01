# PLR §6.4.6: dict subscript with tuple keys.
#
# `dict[tuple[K1, K2, ...], V]` annotation lowers to a dict whose
# keys are tuple-typed, with structural equality on lookup. This
# regression locks in the architecture: the empty-dict literal
# bound to a tuple-keyed annotation is rebuilt to match the
# annotation type (so subsequent subscript writes route through
# tuple-keyed slots), and the existing equal_exprt path in the
# dict subscript scanner handles tuple key comparison.

# Empty annotated dict + tuple subscript-set + tuple subscript-get.
d: dict[tuple[int, int], int] = {}
d[1, 2] = 11
d[3, 4] = 7
assert d[1, 2] == 11
assert d[3, 4] == 7
assert (1, 2) in d
assert (5, 6) not in d

# Construction from a tuple-keyed literal.
d2: dict[tuple[int, int], int] = {(7, 8): 100, (9, 10): 200}
assert d2[7, 8] == 100
assert d2[9, 10] == 200

# get() with default for missing tuple key.
assert d2.get((11, 12), 0) == 0
assert d2.get((7, 8), 0) == 100
