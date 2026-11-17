# add, discard and remove of a set of strings (a list of distinct elements)
# raised AttributeError (add, discard); remove raises KeyError.
def methods(x: str):
    s = {"a", "b"}
    s.add(x)
    assert x in s and "a" in s
    s.discard("a")
    assert "a" not in s or x == "a"
    s.remove("b")
    assert "b" not in s or x == "b"
    try:
        s.remove("zz")
        assert x == "zz"
    except KeyError:
        pass


methods("k")
