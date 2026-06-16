# Precise re.split for a fixed-length pattern on a constant subject, via the
# match-position intrinsics. Enabled by the imported-module default-population
# fix: re.split's `flags` (the last of two trailing defaults) now folds to 0
# when omitted, so the `if flags != 0` soundness guard no longer goes nondet.
import re

r = re.split(",", "a,b,c")
assert len(r) == 3
assert r[0] == "a"
assert r[1] == "b"
assert r[2] == "c"

# No separator match -> the whole string.
assert re.split(",", "abc") == ["abc"]

# maxsplit caps the number of splits.
m = re.split(",", "a,b,c", 1)
assert len(m) == 2
assert m[0] == "a"
assert m[1] == "b,c"

# Two-character separator.
t = re.split("::", "a::b::c")
assert len(t) == 3
assert t[1] == "b"
