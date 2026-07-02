# No-false-positive guard: set-algebra `|`/`&` over set LITERALS (which carry the
# set-semantic marker) must NOT be flagged as a TypeError -- `set | set` is valid.
# Mirrors the crash-typing-py-solver frozenset-union pattern. Also int&int and
# list repeat stay valid.
a = frozenset({"a", "b"})
b = frozenset({"c"})
c = a | b | {"e"}
s = {"x"} | {"y"}
assert (6 & 3) == 2
assert len([1] * 3) == 3
