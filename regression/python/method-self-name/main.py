# PLR 3.3: the instance receiver is the FIRST positional parameter,
# whatever its name -- 'self' is a convention, not syntax. ~20
# converter sites keyed on the literal name (receiver param typing,
# instance-attribute registration incl. TUPLE-target assigns,
# forward-reference scans, unbound-call prepend, signature
# validation); a method using `s` lost attribute binding entirely.
class P:
    def __init__(s, n):
        s.n = n

    def get(s):
        return s.n


class Q:
    def __init__(this, a, b):
        this.a, this.b = a, b     # tuple-target attribute assign

    def total(this):
        return this.a + this.b


p = P(5)
assert p.get() == 5
q = Q(2, 3)
assert q.total() == 5
