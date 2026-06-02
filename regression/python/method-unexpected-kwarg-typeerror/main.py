# PLR §8.7: a bound method call with a keyword that matches no
# parameter (and no **kwargs) raises TypeError. Also exercises an
# MRO-inherited method resolving to the defining class.
class B:
    def m(self, a):
        return a


class D(B):
    pass


d = D()
d.m(b=5)
