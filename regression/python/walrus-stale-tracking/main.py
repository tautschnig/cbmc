# PLR §6.12: the walrus operator `(b := 3)` rebinds b, so its stale tracking must
# be refreshed (like a plain assign). `b = 1` tracks float_constants[b]=1; the
# walrus rebind to 3 was not reflected, so a later `t[b]` folded the index to the
# STALE 1 -- wrong element AND masking the IndexError (t has 3 elements, b is 3).
# A false proof found by direct probing of the stale-tracking whole-group.
b = 1
t = (6, 3, 9)
x = (b := 3)
r = t[b]
