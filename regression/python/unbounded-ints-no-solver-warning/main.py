# --python-unbounded-ints introduces the non-fixed-width mathematical
# integer_typet, which the bit-vector (SAT) backend cannot reason about: it
# requires an SMT solver. When none is selected the frontend emits a warning
# (but still runs). The body here is constant-foldable so the verdict is
# deterministic regardless of backend.

x = 5
assert x == 5
