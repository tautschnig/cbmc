# Python object equality diverges from structural equality. Classes
# with __eq__ + __hash__ take the user-__eq__ DISPATCH tier (see
# python-user-eq-dispatch); classes with DEFAULT equality (identity)
# cannot be modeled for by-value keys -- identity is lost at
# storage -- and are REJECTED loudly (python-model-limitation,
# fail-closed): distinct equal-fielded instances falsely proved
# lookup hits before the guard (constructor TREES matched).
class C:
    def __init__(self, n):
        self.n = n


d = {C(1): 'a'}
x = d[C(1)]          # CPython: KeyError (identity eq)
assert x == 'a'
