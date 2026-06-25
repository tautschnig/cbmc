# Default 64-bit int model: a constant computation that DEFINITELY exceeds the
# signed-64 range is now REPORTED as a python-model-bound (assert+assume cut),
# mirroring the container-capacity guards, instead of silently wrapping to a
# deterministic-wrong value (which false-proved, e.g. `10**19 < 0`). Symbolic
# arithmetic whose overflow can't be decided is left to the documented 64-bit
# bound (use --python-unbounded-ints for full soundness).
x = 10 ** 19
y = x + 1   # unreachable after the model-bound cut on x
assert y == 0
