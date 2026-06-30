# PLR 6.10.1: ordering lists whose concrete element categories are incompatible
# (numeric vs str) raises TypeError at the first compared pair. For uniformly-
# typed lists the mismatch is at index 0.
b = [1, 2] < ["a", "b"]
