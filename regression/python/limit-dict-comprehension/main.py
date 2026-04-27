# PLR §6.2.7: dictionary comprehension
d = {k: k*2 for k in range(3)}
assert d[1] == 2
