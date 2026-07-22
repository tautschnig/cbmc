# PLR §8.3: `continue` in a for-loop must advance to the NEXT iteration --
# i.e. reach the loop's increment. All four convert_for variants emitted
# the induction increment as a body-TAIL statement under a `while`, so a
# `continue` jumped past it into an INFINITE goto loop, silently
# truncated under --no-unwinding-assertions into VACUOUS proofs (ESBMC
# for_range_continue_fail). Now C-style `for` with the increment as the
# iter expression, across range / list / dict / string.
s = 0
for i in range(5):
    if i % 2:
        continue
    s += i
assert s == 6

t = 0
for x in [1, 2, 3, 4]:
    if x == 2:
        continue
    t += x
assert t == 8

n = 0
for c in "abc":
    if c == "b":
        continue
    n += 1
assert n == 2
