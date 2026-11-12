# The element values of a native list comprehension of strings are exact.
r = [x + "!" for x in ["p", "q"]]
assert r[1] == "p!"
