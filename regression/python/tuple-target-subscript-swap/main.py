# PLR §7.2: `a[i], a[j] = a[j], a[i]` -- the RHS tuple is snapshotted
# (evaluation order), then stored. The tuple-unpack loop only handled
# Name/Tuple element targets, so SUBSCRIPT elements were dropped and the
# swap never happened (ESBMC github_4792_fail -- a[0] stayed 1).
a = [1, 2]
a[0], a[1] = a[1], a[0]
assert a[0] == 2 and a[1] == 1

b = [1, 2, 3]
b[0], b[2] = b[2], b[0]
assert b[0] == 3 and b[1] == 2 and b[2] == 1
