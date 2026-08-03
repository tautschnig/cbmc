# PLR §6.4.6 / §6.10.1: dict lookup and sequence search compare by
# VALUE. Pinned with a needle that is genuinely built at RUNTIME
# (loop-concatenated), which defeats every constant fold and hits the
# per-slot scans. Before the container-ops consolidation
# (python_container_ops.cpp), the mutate-side scans — setdefault, pop,
# del, subscript write, list index/count/remove — compared string keys
# by the refined-string DATA POINTER (raw struct equality) and falsely
# refuted all of these, while the read-side scans (get, subscript
# read, in) compared content. One helper now spells the comparison
# for all of them.
k = ""
for c in ["a", "b"]:
    k = k + c

d1 = {"ab": 1, "cd": 2}
assert d1.setdefault(k, 99) == 1

d2 = {"ab": 1, "cd": 2}
assert d2.pop(k) == 1
assert len(d2) == 1

d3 = {"ab": 1, "cd": 2}
del d3[k]
assert len(d3) == 1

d4 = {"ab": 1, "cd": 2}
d4[k] = 7
assert len(d4) == 2
assert d4["ab"] == 7

xs = ["ab", "cd"]
assert xs.index(k) == 0
assert xs.count(k) == 1
xs.remove(k)
assert len(xs) == 1
assert xs[0] == "cd"

# nested write through the same machinery (ordering pin: the equality
# snapshot must be sequenced with the nest temp's population)
m = {"inner": {"x": 10}}
m["inner"]["x"] = 77
assert m["inner"]["x"] == 77
