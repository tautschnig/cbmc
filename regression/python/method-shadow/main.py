# PLR 3.3.2 / 9.4: non-data-descriptor (method) shadowing. An instance
# attribute shadows a same-named method, so after `c.m = 99` reading c.m
# yields 99, not the bound method. A DIFFERENT instance that has not
# shadowed reads the (exact) bound method, which is callable.
class C:
    def m(self) -> int:
        return 1


c = C()
c.m = 99
assert c.m == 99  # shadowed -> the instance value

d = C()
x = d.m  # unshadowed -> the exact bound method (not nondet)
assert x() == 1  # ... which is callable and returns 1
