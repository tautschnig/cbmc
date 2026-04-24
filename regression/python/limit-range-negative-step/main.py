# Python Language Reference §4.6.6: range with step
# range(1, 5, -1) produces empty sequence
x = 0
for y in range(1, 5, -1):
    x = y
assert x == 0
