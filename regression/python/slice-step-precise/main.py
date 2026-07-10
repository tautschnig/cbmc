# PLR §6.3.3: constant-step slicing over a constant list is folded precisely per
# Python's slice.indices -- for default AND literal-constant bounds, positive and
# negative step. Follow-up to the sound over-approximation (slice-step-unsupported).
assert [1, 2, 3, 4, 5][::2] == [1, 3, 5]
assert [1, 2, 3, 4, 5][::-2] == [5, 3, 1]
assert [1, 2, 3][::-1] == [3, 2, 1]
assert [1, 2, 3, 4, 5][1:5:2] == [2, 4]
assert [1, 2, 3, 4, 5][4:0:-2] == [5, 3]
assert [1, 2, 3, 4, 5][-1::-2] == [5, 3, 1]
assert [1, 2, 3, 4, 5][10:20:2] == []
assert len([1, 2, 3, 4, 5][::2]) == 3
