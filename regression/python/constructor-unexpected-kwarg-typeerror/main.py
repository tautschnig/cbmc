# PLR §8.7: constructing with a keyword that matches no __init__
# parameter (and no **kwargs) raises TypeError. Also exercises an
# MRO-inherited __init__ resolving to the defining base class.
class B:
    def __init__(self, a):
        self.a = a


class D(B):
    pass


d = D(b=5)
