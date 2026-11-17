# A set[str] parameter is a set.
def m(t: set[str]):
    t.add("q")
    assert "q" in t
    t.discard("q")
    assert "q" not in t


m({"m"})
