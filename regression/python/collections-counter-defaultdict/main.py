# Smoke test for collections.Counter and
# collections.defaultdict.
# Our frontend models these at the Python level; precise
# tracking of _items across method calls is an
# approximation, so the assertions here are limited to
# structural smoke-tests.


from collections import Counter, defaultdict

# Just verify the classes can be imported and instantiated.
c = Counter([1, 2, 3])
d = defaultdict(int)
d2 = defaultdict(list)

# Default None-sentinel for unknown factory.
d3 = defaultdict()
assert d3.__missing__("x") is None
