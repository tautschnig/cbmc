# PLR §6.10.2: min()/max() must evaluate each argument exactly ONCE. Two bugs
# combined: (1) they duplicate operands across the pairwise-comparison fold, and
# (2) the 2-arg mixed-type case re-converted its operands (elems loop + legacy
# fallback), so a side-effecting call arg like `o.bump()` ran TWICE, reading a
# stale value -- a false proof found by the mutation-oracle. Operands are now
# converted once (materialised) and reused. The mutation count is deterministic.
class C:
    def __init__(self):
        self.w = 0
    def bump(self):
        self.w = self.w + 1
        return self.w
o = C()
m = max(o.bump(), 0)      # bump() -> 1, called once
assert o.w == 1
assert min(5, 9, 2) == 2
