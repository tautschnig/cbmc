# twin: the demonstrated false proof -- equal-fielded DISTINCT
# instances must not compare `is`-identical.
class C:
    def __init__(self, n):
        self.n = n


a = C(1)
c = C(1)
assert a is c
