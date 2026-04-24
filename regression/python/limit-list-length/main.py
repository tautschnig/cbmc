# Limitation: lists bounded to PYTHON_MAX_LIST_LENGTH (64)
# This test verifies that lists up to the bound work correctly
lst = [1, 2, 3, 4, 5]
r = lst * 2
assert len(r) == 10
assert r[5] == 1
# Lists longer than 64 elements would overflow
