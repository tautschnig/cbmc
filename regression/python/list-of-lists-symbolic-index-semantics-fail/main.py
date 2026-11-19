bins = [[] for k in range(4)]
got = []
for j in [-1, 2, 5, -5]:
    try:
        bins[j].append(j)
        got.append(len(bins[j]))
    except IndexError:
        got.append(-9)
assert got == [1, 1, 1, -9] and bins[3] == [-1] and bins[2] == [2]
