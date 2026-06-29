# PLR §3.3.1: subscripting an instance requires __getitem__. A class that (across
# its MRO) defines no __getitem__ -> `C()[0]` raises TypeError ('object is not
# subscriptable'). Previously fell through to a nondet read.
class C:
    pass


v = C()[0]
