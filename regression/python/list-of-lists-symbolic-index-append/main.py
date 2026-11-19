bins = [[] for k in range(16)]


def route(slot: int, val: int) -> int:
    bins[slot].append(val)
    acc = 0
    for b in range(16):
        while bins[b]:
            acc += bins[b].pop(0)
    return acc


w = nondet_string(16)
for c in w:
    __CPROVER_assume(c in "0123456789abcdef")
__CPROVER_assume(w[0] in "01234567")
v = int(w, 16)
k = nondet_int()
__CPROVER_assume(k >= 0 and k < 16)
assert route(k, v) == v
