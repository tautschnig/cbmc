# An unannotated empty dict {} defaults to dict[str, int]; assigning
# int keys used to lossily coerce them to the string key type, so
# distinct int keys collapsed and the "found" search wrongly matched
# (len/value corruption). The first d[k] = v now rebuilds the empty
# dict with the actual key/value types.
d = {}
d[0] = 10
d[1] = 20
d[2] = 30
assert len(d) == 3
assert d[1] == 20
assert d[2] == 30
