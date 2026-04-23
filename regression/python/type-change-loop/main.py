# Case E: loop with type change — needs tagged union in loop body
x = 0
for item in [1, 2, 3]:
    x = item
assert x == 3
# Here x stays int throughout, but the general pattern could involve
# type changes if the list were heterogeneous.
