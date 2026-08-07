# The user-__eq__ dispatch tier (the loop-based fallback the
# quantified encodings cannot host): class keys whose class defines
# __eq__ AND __hash__ get REAL Python key semantics via a
# materialised __eq__ call per slot -- construction dedup IS the
# replace-or-insert store semantics (PLR 6.2.7: key object +
# position from the FIRST occurrence, value from the LAST), lookups
# honor the user equality, and __eq__-without-__hash__ raises
# TypeError (unhashable) at key insertion.
class K:
    def __init__(self, n, tag):
        self.n = n
        self.tag = tag

    def __hash__(self):
        return hash(self.n)

    def __eq__(self, o):
        return isinstance(o, K) and self.n == o.n


pairs = [(K(1, 'first'), 'v1'), (K(2, 'x'), 'v2'), (K(1, 'second'), 'v3')]
d = {k: v for k, v in pairs}
assert len(d) == 2
assert d[K(1, '?')] == 'v3'      # custom eq finds; LAST value won
assert d[K(2, '?')] == 'v2'
