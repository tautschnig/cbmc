# Each assertion fails in CPython. They verified: add of a set of strings
# (a list) and isdisjoint of a dict view raised AttributeError, so the
# handler made the assertion unreachable; update and union dropped the new
# elements; list(s)[0] was the first element inserted.
def handled():
    s = {"a", "b"}
    try:
        s.add("c")
        assert False, "add"
    except AttributeError:
        pass


def view():
    k = {"a": 1}.keys()
    try:
        k.isdisjoint({"x"})
        assert False, "isdisjoint"
    except AttributeError:
        pass


def update():
    s = {"a", "b"}
    s.update({"c"})
    assert "c" not in s, "update"


def union():
    u = {"a", "b"}.union({"c"})
    assert "c" not in u, "union"


def order():
    s = {"a", "b"}
    assert list(s)[0] == "a", "list order"


handled()
view()
update()
union()
order()
