# PLR §3.3.1: a comprehension over a non-iterable class instance (no __iter__,
# no __getitem__) raises TypeError, like the for-loop.
class C:
    pass


xs = [x for x in C()]
