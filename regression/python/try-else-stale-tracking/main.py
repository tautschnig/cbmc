# CORE (PLR §8.4): an assignment in a try's `else` clause updates tracking, but
# the try/except/else arm-merge reflects only the try/except post-states and
# DISCARDED the else's updates -- so a stale constant survived: `b = 1`;
# `else: b = 9`; then `t[b]` folded the index on the stale b=1 (t[1] in bounds),
# masking the IndexError (t has 2 elements, b is 9). CPython: b=9, t[9] raises
# IndexError -> VERIFICATION FAILED. Fixed by clearing tracking for else-assigned
# names after the merge (mirrors the finally-clause fix).
b = 1
xs = [2, 9, 8, 2]
t = (9, 6)
try:
    a = xs[3]
except IndexError:
    a = 0
else:
    b = 9
r = t[b]
