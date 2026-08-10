# --python-ref-instances: construction heap-allocates and binds a
# POINTER local (rebinding allocates fresh; the old object stays
# live -- PLR 3.1). Default-equality class keys then use IDENTITY =
# pointer equality: distinct equal-fielded objects don't dedup,
# same-object/alias lookups hit, mutation doesn't change identity.
class C:
    def __init__(self, n):
        self.n = n


k1 = C(1)
k2 = C(1)
d = {k1: 'a', k2: 'b'}
assert len(d) == 2
assert d[k1] == 'a'
assert d[k2] == 'b'
alias = k1
assert d[alias] == 'a'
assert k1 in d
k1.n = 99
assert d[k1] == 'a'      # identity survives mutation
e = C(1)
assert e not in d        # fresh object never in
old = k2
k2 = C(1)                # rebind: fresh object
assert k2 not in d       # the dict holds the OLD object...
assert d[old] == 'b'     # ...reachable through the surviving name
