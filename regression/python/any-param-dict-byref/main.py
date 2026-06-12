def mutate(d):
    # d is unannotated -> Any / python_value parameter. The dict is shared
    # by reference, so this mutation must propagate back to the caller.
    d["k"] = 9


m = {"k": 1}
mutate(m)
# CPython: m["k"] == 9 after the by-reference mutation. This previously
# either crashed under --python-smt-strings (byte-unpacking the
# smt_string-keyed dict reached via the opaque __class_ptr) or silently
# read the stale value (a false proof of m["k"] == 1).
assert m["k"] == 9
