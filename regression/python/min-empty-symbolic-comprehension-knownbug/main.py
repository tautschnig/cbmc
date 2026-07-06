# KNOWNBUG: min()/max() on an empty sequence raises ValueError, and the check
# (arg.length > 0) is sound for literal-empty, slice-empty, and constant-range
# comprehension-empty lists (all caught). But when the list length flows through
# a comprehension over range(SYMBOLIC) then a slice --
# `xs = sorted([5]); xs = [i for i in range(len(xs))][1:5]` -- the resulting
# length is mismodeled (not tracked to 0), so the empty check is vacuously
# satisfied and min(xs) false-proves SUCCESSFUL. ROOT: upstream list-length
# tracking through range(symbolic)-comprehension + slice, NOT min/max. Found by
# the property-based random fuzzer (rand_172).
xs = sorted([5])
xs = [i for i in range(len(xs))][1:5]
r = min(xs)
