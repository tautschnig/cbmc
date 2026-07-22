# The refutation half: the sum of the EVEN values of range(5) is 6, not
# 5 -- and previously the continue-induced infinite-goto vacuity PROVED
# whatever was asserted. Must fail.
s = 0
for i in range(5):
    if i % 2:
        continue
    s += i
assert s == 5
