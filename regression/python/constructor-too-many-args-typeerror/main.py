# PLR §8.7: constructing with more positional arguments than
# __init__ accepts raises TypeError (the new instance is the implicit
# self).
class C:
    def __init__(self, a):
        self.x = a


c = C(1, 2)
