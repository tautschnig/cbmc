# PLR §3.3.2: assigning `o.attr = ...` CREATES the attribute, so it is NOT a
# missing-attribute access. The default-on Any-arg-attribute check (which flags a
# READ of an attribute the concrete class lacks, on an Any-typed parameter) must
# NOT flag a STORE (nor a read of an attribute created by an earlier store).
class C:
    def __init__(self):
        self.a = 1

    def m(self):
        return 1


def set_new(o):
    o.x = 5           # creates x -> not a missing-attribute access


def set_from_read(o):
    o.y = o.a         # reads o.a (exists), creates o.y


def use_present(o):
    return o.m() + o.a  # method + declared attr -> present


c = C()
set_new(c)
set_from_read(c)
assert use_present(c) == 2
