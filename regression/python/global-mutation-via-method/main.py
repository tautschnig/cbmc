# A method (and transitive call chains) that mutate a module global must
# be reflected at a later module read -- the single Call chokepoint
# invalidates global scalar + dict tracking for any call form.
d = {"a": 1}
cfg = "production"


class C:
    def add(self) -> None:
        d["b"] = 2

    def reconfig(self) -> None:
        global cfg
        cfg = "overload"


c = C()
c.add()
assert len(d) == 2 and d["b"] == 2
c.reconfig()
assert cfg == "overload"
