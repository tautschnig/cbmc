# No-false-positive guard for chained assignment: mutable targets alias (share
# identity), immutable targets are independent after rebinding, and 3-way chains
# work. All assertions below hold in CPython.
class C:
    def __init__(self):
        self.v = 1


a = b = c = C()  # 3-way alias
c.v = 7
assert a.v == 7 and b.v == 7

p = q = []  # list alias
p.append(1)
assert len(q) == 1

x = y = 0  # immutable: rebinding x does not affect y
x = 5
assert y == 0
