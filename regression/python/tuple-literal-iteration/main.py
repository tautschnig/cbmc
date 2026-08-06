# for-over-tuple-literal unrolls per field (PLR 8.3); previously the
# loop variable was a single NONDET assignment (every derived
# count/sum false-alarmed).
s = 0
for rid in (6, 11, 17):
    s += rid
assert s == 34

c = 0
for rid in (6, 11, 17):
    if rid % 3 == 0:
        c += 1
assert c == 1

# heterogeneous elements: var retypes per iteration
acc = 0
for v in (1, True, 2):
    if v:
        acc += 1
assert acc == 3
