# PLR §7.2.1: unpacking a fixed-length list requires its length to equal the
# number of targets, else ValueError. The list-rhs unpack ASSUMED len==count
# (vacuously verifying a mismatch); for a constant-length list literal the
# length folds, so a mismatch is now a definite ValueError. Symbolic-length
# lists keep the assume (no spurious failure); starred/correct unaffected.
def main() -> None:
    a, b, c = [1, 2]   # ValueError: not enough values to unpack


main()
