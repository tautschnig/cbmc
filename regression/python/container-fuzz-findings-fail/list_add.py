# The assertion fails in CPython.
# A list has no add: it was the set method.
l = ["a"]
k = "ok"
try:
    l.add("a")
except AttributeError:
    k = "AttributeError"
assert k == "ok", "list_add"
