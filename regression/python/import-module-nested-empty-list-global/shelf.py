bins = [[] for k in range(3)]


def parse(tok):
    return int(tok, 16)


def load(cols):
    for j in range(len(cols['T'])):
        bins[1].append(cols['T'][j])
    acc = 0
    while bins[1]:
        acc = acc + parse(bins[1].pop(0))
    return acc
