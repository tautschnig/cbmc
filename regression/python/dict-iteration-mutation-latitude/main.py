# Documented latitude: iterating a dict reads a SNAPSHOT of its
# keys (insertion-ordered); CPython RAISES RuntimeError on
# mutation during iteration, which the model does not raise. The
# facts provable from the snapshot are facts of the pre-iteration
# dict -- sound for programs that do not mutate mid-iteration, and
# this test pins the CURRENT behaviour so a future RuntimeError
# model shows up as an expected diff.
d = {1: 'a', 2: 'b'}
ks = [k for k in d]
assert len(ks) == 2
assert ks[0] == 1
