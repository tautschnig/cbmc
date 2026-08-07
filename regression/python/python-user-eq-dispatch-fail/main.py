# twins: (1) the un-deduped length must NOT prove; (2) an
# always-False __eq__ makes the unguarded lookup raise (KeyError
# reachable); (3) __eq__ without __hash__ is unhashable (TypeError).
class K:
    def __init__(self, n):
        self.n = n

    def __hash__(self):
        return hash(self.n)

    def __eq__(self, o):
        return isinstance(o, K) and self.n == o.n


d = {k: k.n for k in [K(1), K(2), K(1)]}
assert len(d) == 3
