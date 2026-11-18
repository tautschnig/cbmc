# The same programs, with the assertions that hold in CPython.
xs = [1]
assert (xs.pop() if xs else 0) == 1
assert not set()
assert {1}
assert ",".join(sorted(set())) == ""


def f():
    s = 0
    s = ["a"]
    s = ["b"]
    assert s[0] == "b"


f()
u = {"a"}
assert not (u <= set()) and set() <= u and u < {"a", "b"} and not (u < u)
assert set().union({"a", "b"}) == {"a", "b"}
