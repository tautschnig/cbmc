# Soundness: the set is a 64-bit int bitmap; a non-int element (tuple) cannot be
# represented. It must neither false-prove membership (a deterministic bit could
# collide) NOR false-prove len() (popcount of the bitmap). Adding (1,2) havocs
# the bitmap, so both these genuinely-false assertions must produce VERIFICATION
# FAILED.
s = set()
s.add((1, 2))
assert len(s) == 0     # len() must not be provably 0 after an add
assert (3, 4) in s     # membership of a different element must not be proved
