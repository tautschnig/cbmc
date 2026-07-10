# PLR §3.1: an assignment inside a try body with a side-effecting/checked RHS
# (`a = xs[1]`) takes the try-split path, which returned early and skipped the
# scalar-constant clearing the straight-line assign does. So `a = 1` left
# float_constants[a]=1, and after the try a later `t[a]` folded the index to the
# STALE 1 (wrong element + masking the IndexError: t has 3 elements, a is 4). A
# false proof found by the mutation-oracle. The try-split path now clears it.
a = 1
xs = [7, 4, 2, 6]
t = (9, 0, 4)
try:
    a = xs[1]
finally:
    b = 0
r = t[a]
