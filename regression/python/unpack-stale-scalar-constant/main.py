# PLR §3.1: a tuple-unpacking target's scalar constant must be cleared. `b = 1`
# tracks float_constants[b]=1; `a, b = (t[0], t[1])` reassigns b to 3 but the
# unpack path did not clear the scalar constant, so a later `t[b]` constant-
# folded to t[1] using the STALE 1 -- wrong element AND masking the IndexError
# (t has 3 elements, t[3] is out of range). A false proof found by the mutation-
# oracle. b is now untracked after unpack, so t[3] raises IndexError.
b = 1
t = (6, 3, 9)
a, b = (t[0], t[1])
r = t[b]
