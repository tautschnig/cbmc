# P1 of doc/python-frontend-unbounded-containers-plan.md
# (--python-smt-containers): lists carry an INFINITE data array — the
# core operations (construction, append, index incl. negative,
# len, iteration, comprehension, slice, sort, equality) are EXACT at
# any length, with no python-model-bound capacity property.
xs = []
i = 0
while i < 20:
    xs.append(i)
    i += 1
assert len(xs) == 20
assert xs[19] == 19
assert xs[-1] == 19

ys = [n * 2 for n in range(30)]
assert len(ys) == 30
assert ys[29] == 58

zs = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19]
assert zs[17] == 17

total = 0
for x in [1, 2, 3]:
    total += x
assert total == 6

sl = ys[2:5]
assert len(sl) == 3
assert sl[0] == 4

ws = [3, 1, 2]
ws.sort()
assert ws == [1, 2, 3]
