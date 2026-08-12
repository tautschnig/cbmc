# PLR 6.10.1: iterating a dict yields KEYS in INSERTION order; the
# dict struct's keys[] array is insertion-ordered with build-time
# dedup, so a dict iterable lowers to a keys-list view -- closed
# forms, filters, and unrolling all apply unchanged.
d = {3: 'a', 1: 'b', 2: 'c'}
ks = [x for x in d]
assert len(ks) == 3
assert ks[0] == 3
fs = [x for x in d if x != 1]
assert len(fs) == 2
assert fs[0] == 3
