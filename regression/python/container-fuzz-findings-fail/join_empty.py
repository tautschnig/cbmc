# The assertion fails in CPython.
# str.join of an empty list of non-strings (sorted(set())) raised
# TypeError, so a handler made the code after it unreachable.
def f() -> str:
    return ",".join(sorted(set()))


k = "ok"
try:
    f()
except TypeError:
    k = "TypeError"
assert k != "ok", "join_empty"
