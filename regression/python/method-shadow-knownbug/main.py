# differential2 §11b: non-data-descriptor (method) shadowing. An
# instance attribute shadows a same-named method (a non-data
# descriptor), so after `c.m = 99` reading c.m yields 99, not the bound
# method. CBMC keeps methods on the call path, not as shadowable
# instance-dict entries, so this is not modeled.
class C:
    def m(self) -> int:
        return 1


c = C()
c.m = 99
assert c.m == 99
