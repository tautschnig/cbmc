# PLR §8.7: a bound method call with too many positional arguments
# (the implicit self plus the explicit args exceed the parameters)
# raises TypeError.
class C:
    def m(self, a):
        return a


c = C()
c.m(1, 2)
