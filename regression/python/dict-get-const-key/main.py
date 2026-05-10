# dict.get() with a string-literal key and a dict-literal
# value resolves at conversion time (no loop-over-slots at
# symex).


d = {"a": 1, "b": 2, "c": 3}

# Present keys: resolve to the literal value.
assert d.get("a") == 1
assert d.get("b") == 2
assert d.get("c") == 3

# Missing key: resolve to the default.
assert d.get("x", 99) == 99

# Default None-sentinel when no default provided.
none_sentinel = -4611686018427387904
assert d.get("y") == none_sentinel
