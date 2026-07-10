# PLR §8.3: the for-loop target is reassigned each iteration, so its tracking
# must be invalidated. `b = 1` tracks b=1; `for b in [3]` rebinds b to 3, but a
# later `t[b]` folded on the stale b=1 (wrong element + masked IndexError, t has
# 3 elements). Found by proactively probing the reassignment whole-group after
# consolidating the invalidation into one helper.
b = 1
t = (6, 3, 9)
for b in [3]:
    pass
r = t[b]
