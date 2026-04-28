# PLib builtins: range() as expression (not in for-in)
lst = list(range(5))
assert len(lst) == 5
assert lst[0] == 0
