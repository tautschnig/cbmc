# Tier 2 / Case D: conditional type change — merge point ambiguity
# In Python, x is int on one path and str on the other.
# At the merge point, the type of x is ambiguous.
flag: bool = True
if flag:
    x = 42
else:
    x = "hello"
# At this point, x is 42 (since flag is True)
assert x == 42
