# The assertion fails in CPython.
# A variable whose type changed was assigned at its first version after
# its second assignment, so reads kept the second value.
def f():
    s = 0
    s = ["a"]
    s = ["b"]
    assert s[0] != "b", "versions"


f()
