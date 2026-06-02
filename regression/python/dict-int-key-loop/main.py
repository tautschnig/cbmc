# Creation-time look-ahead for unannotated empty dicts: a `d = {}`
# whose first `d[k] = v` (found by the body pre-scan, even inside a
# loop) determines the key/value element types builds `{}` with those
# types from the start. So an int-keyed dict filled in a range loop is
# modeled correctly (key type int, value type inferred from i * 2),
# without needing an explicit `d: dict[int, int] = {}` annotation.
d = {}
for i in range(8):
    d[i] = i * 2
assert len(d) == 8
assert d[3] == 6
assert d[7] == 14
