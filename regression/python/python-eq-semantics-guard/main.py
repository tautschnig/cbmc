# Python object equality (identity by default, user __eq__ when
# defined) diverges from the model's STRUCTURAL equality in both
# directions -- four demonstrated FALSE PROOFS before the guard:
# missing dedup proved len(d) == 3 where custom __eq__ dedups to 2;
# structural lookup/membership folds proved no-KeyError / `in` where
# CPython raises / says False (constructor TREES matched, but
# distinct instances are UNEQUAL under identity-eq). Class-typed
# dict keys and list-membership operands are REJECTED loudly
# (python-model-limitation, fail-closed) until user-__eq__ dispatch
# is modeled.
class K:
    def __init__(self, n, tag):
        self.n = n
        self.tag = tag

    def __hash__(self):
        return hash(self.n)

    def __eq__(self, o):
        return isinstance(o, K) and self.n == o.n


d = {k: v for k, v in [(K(1, 'a'), 1), (K(2, 'b'), 2), (K(1, 'c'), 3)]}
assert len(d) == 3
