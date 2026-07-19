# §0 write-through DEMOTION discipline: any statement touching the source
# dict between extraction and mutation invalidates the recorded slot
# index, so the write-through must NOT fire (a stale-index write would be
# a FALSE WRITE -- worse than the sound havoc it replaces). Each hazard
# below ends in a wrong-value assert that must stay UNPROVABLE: the
# demoted path havocs, so the asserts FAIL (sound over-approximation).
# Hazards: same-key overwrite (write-through would resurrect the old
# object) and new-key insertion (values[] may be restructured).
d = {1: [1]}
v = d[1]
d[1] = [99]  # same-key overwrite: demotes v's slot alias
v.append(2)
assert d[1][0] == 1  # CPython: 99 -> AssertionError; must not prove
