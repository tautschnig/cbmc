# Unifying the free-function and method return-type scanners gave
# methods the tuple-return inference free functions already had. A
# method returning `a, b` with no annotation now types as a tuple
# (previously int), so unpacking the result yields the right values.
class Stats:
    def minmax(self):
        return 3, 9


s = Stats()
lo, hi = s.minmax()
assert lo == 3
assert hi == 9
