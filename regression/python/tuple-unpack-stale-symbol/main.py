# PLR §3.1: a tuple-unpacking assignment `a, b = ...` reassigns a and b, so any
# cached literal referencing them must be invalidated. `t = (b, a)` caches
# symbols b,a; after `a, b = (9, 9)` a fold reusing that snapshot read the NEW
# a/b -> false proof (found by the mutation-oracle; the unpack path missed the
# invalidation that plain/ann assigns already had). r stays (2,0), so `!= (2,0)`
# raises.
b = 2
a = 0
t = (b, a)
a, b = (9, 9)
r = t * 1
assert r != (2, 0)
