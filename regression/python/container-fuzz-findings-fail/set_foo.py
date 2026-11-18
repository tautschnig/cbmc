# The assertion fails in CPython.
# A method neither sets nor lists have, on a set held in a list, raised
# nothing.
s = {"c"}
k = "ok"
try:
    s.foo()
except AttributeError:
    k = "AttributeError"
assert k == "ok", "set_foo"
