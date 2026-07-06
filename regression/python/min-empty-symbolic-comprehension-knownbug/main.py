# KNOWNBUG (PLR §6.10.1/§6.3.3): min()/max() on an empty sequence raises
# ValueError; the check (arg.length > 0) is sound and catches literal-empty,
# slice-empty, and CONSTANT-bound comprehension-empty lists. The single open case
# is a comprehension over range(a NON-constant int bound) THEN a slice:
#   xs = sorted([5]); xs = [i for i in range(len(xs))][1:5]; min(xs)
# Localized: the comprehension is UNROLL-based (enumerates constant iteration
# values); with a symbolic bound (`len(sorted([5]))`, provably 1 but not a
# constant) it cannot unroll, so the result length is mismodeled (too large) and
# the subsequent slice's emptiness is missed. `[i for i in range(n)][1:5]` with a
# tracked-constant n=1 IS caught. Closing it needs symbolic-LENGTH range
# comprehension modeling (length = clamp(bound, 0, capacity), nondet data) in the
# unroll-based comprehension path -- the list-length/identity whole-group. Found
# by the property-based random fuzzer (rand_172).
xs = sorted([5])
xs = [i for i in range(len(xs))][1:5]
r = min(xs)
