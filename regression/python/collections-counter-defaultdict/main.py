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

# No factory: behaves like a plain dict for explicit writes.
# (CPython: defaultdict().__missing__('x') RAISES KeyError -- the
# previous `is None` assertion here matched a removed library-model
# artifact, not Python.)
d3 = defaultdict()
d3["a"] = 42
assert d3["a"] == 42
