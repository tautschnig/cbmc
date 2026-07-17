# PLR §6.4/§3.1 dict-value-by-reference, string keys (plan §1 phase 3 /
# dict-value-byref plan): `d["k"].append(...)` mutates the stored list IN
# PLACE and later reads observe it. The subscript returns the lvalue
# values[found_idx] slot (string keys enabled: the slot chain shares the
# value chain's string_equal predicates), and a statement-level pre-scan
# erases the tracked literal before a `X[k].mutator(...)` statement so the
# constant-key fold cannot hand out a stale snapshot -- reads BEFORE the
# first mutation keep full fold precision (see dict18/dict19 in the sweep).
# Cross-slot isolation: mutating d["a"] must not disturb d["b"].
d = {"a": [1], "b": [5]}
assert len(d["a"]) == 1  # pre-mutation read: full fold precision
d["a"].append(2)
assert len(d["a"]) == 2  # mutation observed
assert len(d["b"]) == 1  # cross-slot isolation
assert d["b"][0] == 5
assert d["a"][0] == 1
assert d["a"][1] == 2
