# Obligation-subterm sharing: the dict-subscript lvalue slot's matched
# index is MATERIALISED into a temp (one occurrence of the string_equal
# selection chain instead of one per use -- the duplication multiplied
# string-refinement solver load ~8x per assert on aws_untagged).
# Semantics unchanged: in-place mutation through the slot, cross-slot
# isolation, and staleness all behave exactly as before.
d = {"a": [1], "b": [5]}
d["a"].append(2)
assert len(d["a"]) == 2
assert len(d["b"]) == 1
assert d["a"][1] == 2
assert d["b"][0] == 5
