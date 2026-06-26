# KNOWNBUG (false proof): sequence repetition requires an int count; a float
# count raises TypeError in CPython ("can't multiply sequence by non-int of type
# 'float'"). cbmc silently succeeds.
x = "abc" * 2.5
assert True      # unreachable in CPython
