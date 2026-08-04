# The scan bound is fail-closed AND runtime-configurable: 20 keys
# exceed the default cap (loud python-model-bound), and verify
# EXACTLY with --python-max-dict-size 32 -- with constant symex
# width either way (the infinite arrays never grow the struct).
d = {"k0": 0, "k1": 1, "k2": 2, "k3": 3, "k4": 4, "k5": 5, "k6": 6, "k7": 7, "k8": 8, "k9": 9, "k10": 10, "k11": 11, "k12": 12, "k13": 13, "k14": 14, "k15": 15, "k16": 16, "k17": 17, "k18": 18, "k19": 19}
assert d["k5"] == 5
assert len(d) == 20
