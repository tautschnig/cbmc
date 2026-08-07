class P:
    def __init__(s, n):
        s.n = n

    def get(s):
        return s.n


p = P(5)
assert p.get() == 5
