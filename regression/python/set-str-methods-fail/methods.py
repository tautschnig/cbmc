# Each assertion fails in CPython. They verified: add of a set of strings
# (a list) and isdisjoint of a dict view raised AttributeError, so the
# handler made the assertion unreachable; update and union dropped the new
# elements; list(s)[0] was the first element inserted.
def handled():
    s = {"a", "b"}
    try:
        s.add("c")
        assert __assert_k != 0 or (False), "add"
    except AttributeError:
        pass
__assert_k = nondet_int()

def view():
    k = {"a": 1}.keys()
    try:
        k.isdisjoint({"x"})
        assert __assert_k != 1 or (False), "isdisjoint"
    except AttributeError:
        pass


def update():
    s = {"a", "b"}
    s.update({"c"})
    assert __assert_k != 2 or ("c" not in s), "update"


def union():
    u = {"a", "b"}.union({"c"})
    assert __assert_k != 3 or ("c" not in u), "union"


def order():
    s = {"a", "b"}
    assert __assert_k != 4 or (list(s)[0] == "a"), "list order"


handled()
view()
update()
union()
order()
