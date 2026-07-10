# PLR §7.2.2: `b += ..` rebinds b, so cached literals REFERENCING b must be
# dropped. `t = (b, 0)` caches symbol b=2; `b += 7` (b=9) left tuple_literals[t]
# referencing the stale b, so `t * 1` reused b=9 -> r=(9,0) != (2,0) proved (a
# false proof found by the reassignment-invariant audit). r is (2,0), so
# `!= (2,0)` raises.
b = 2
t = (b, 0)
b += 7
r = t * 1
assert r != (2, 0)
