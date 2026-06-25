# Element-store coercion (P2 audit lock-in): storing a value whose type differs
# from a container's inferred element type must coerce the value faithfully
# (not bit-reinterpret it). Covers the sites confirmed correct by the audit;
# `list.extend` (the one gap found) is covered by ref-mutables-boundaries.
# See doc/python-frontend-reference-semantics-spike.md §13a and the plan doc.

# dict value store: replacing an int value with a string must read back as the
# string (cross-type value store coerces).
d = {"k": 1}
d["k"] = "s"
assert d["k"] == "s"

# *args packing: a heterogeneous argument tuple preserves each element value.
def pick(*a):
    return a[1]
assert pick(1, "s", 3) == "s"

# Soundness: a cross-type store must NOT let a genuinely-false read be proved.
# (corruption, if any, must be nondet/over-approximate, never a false witness.)
d2 = {"k": 1}
d2["k"] = "s"
assert d2["k"] != "wrong"
