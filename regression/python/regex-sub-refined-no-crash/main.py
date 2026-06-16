# Regression for the Tier-0 bounded_nondet_string fix: on the refined (default)
# string backend, the re.* nondet-string fallbacks must NOT route an smt_string
# into the refinement string solver (which has no axioms for it and aborts in
# add_axioms_for_length). re.sub on the default backend used to core here.
# This test deliberately uses the DEFAULT backend (no --python-smt-strings).
import re

# re.sub fallback (smt_string intrinsic is native-only; refined gets a sound
# nondet string struct).
s = re.sub("a", "b", "banana")
assert len(s) >= 0

# Other nondet-string fallbacks on the default backend.
parts = re.split(",", "a,b,c")
assert len(parts) >= 0

for m in re.findall("a", "aaa"):
    assert len(m) >= 0
