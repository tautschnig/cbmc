# Soundness: a string-keyed dict value is returned by COPY (not a writable
# lvalue like int-keyed values), so an in-place mutation `d["k"].append(...)`
# is lost. Reading the stale pre-mutation value used to false-prove
# (len(d["k"])==1 after an append). The dict is now havoced on such a mutation,
# so this genuinely-false assertion must produce VERIFICATION FAILED.
d = {"k": [1]}
d["k"].append(2)
assert len(d["k"]) == 1
assert 2 not in d["k"]
