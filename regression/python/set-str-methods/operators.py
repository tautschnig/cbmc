# The operators | & - ^ on sets of strings, also with a set of ints (set()):
# s - set() raised TypeError (the bitmap set was "incompatible" with the
# list holding the strings), and a list display is no set.
def f() -> str:
    s = {x + "a" for x in ["a", "a", "a"]}
    t = set()
    s = s - t
    return ",".join(sorted(s))


assert f() == "aa"
u = {"a", "b"}
assert u | {"c"} == {"a", "b", "c"}
assert u - set() == u and u | set() == u and len(u & set()) == 0
try:
    w = ["a"] | {"b"}
    assert False
except TypeError:
    pass
