# PLR 6.10.3: `is` on class instances is IDENTITY. Three tiers:
# alias-chain (definitional), pointer equality (by-ref bindings),
# sound nondet for mixed representations. Previously instances fell
# through to VALUE equality -- C(1) is C(1) with equal fields
# falsely PROVED.
class C:
    def __init__(self, n):
        self.n = n


def same(x: C, y: C) -> bool:
    return x is y


a = C(1)
b = a
assert a is b            # alias
assert same(a, a)        # same object through two by-ref params
c = C(1)
assert not same(a, c)    # distinct objects, equal fields
