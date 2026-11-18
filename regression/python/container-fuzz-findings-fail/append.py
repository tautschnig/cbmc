# The assertion fails in CPython.
# A set has no append: it was a list method on the list holding it.
s = set(["", "a"])
k = "ok"
try:
    s.append("a")
except AttributeError:
    k = "AttributeError"
assert k == "ok", "append"
