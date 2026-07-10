# PLR §6.3.3: a step-1/reverse slice over a constant list now yields a CONSTANT
# struct (like the concat/step folds), so a CHAINED slice folds precisely.
assert [1, 2, 3, 4, 5][1:4][::-1] == [4, 3, 2]
assert [1, 2, 3, 4, 5][0:4][::2] == [1, 3]
assert [1, 2, 3, 4, 5][-4:][::-2] == [5, 3]
assert sorted([3, 1, 2, 4][1:4]) == [1, 2, 4]
assert [1, 2, 3, 4][1:-1] == [2, 3]
