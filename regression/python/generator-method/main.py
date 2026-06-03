# PLR §6.2.9: a method containing `yield` is a generator method. It must
# get the same eager-result-list machinery free-function generators get,
# so both for-loop iteration and next() work.
class Source:
    def gen(self):
        yield 10
        yield 20
        yield 30


s = Source()

total = 0
for x in s.gen():
    total += x
assert total == 60

g = s.gen()
assert next(g) == 10
assert next(g) == 20
