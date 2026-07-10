# PLR §6.2: abs() must evaluate its argument exactly ONCE. abs() duplicates its
# operand across the tag/sign dispatch branches; a side-effecting argument (a
# mutating method call) was re-executed per branch, reading a stale value -- a
# false proof found by the mutation-oracle (`abs(o.bump())`). The operand is now
# materialised into a temp first.
class C:
    def __init__(self):
        self.w = 0
    def bump(self):
        self.w = self.w + 1
        return self.w
o = C()
assert abs(o.bump()) == 1
assert o.w == 1
