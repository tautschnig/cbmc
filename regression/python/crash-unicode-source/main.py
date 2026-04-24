# String concatenation content tracking — not a Unicode issue.
# "α" + "β" has correct length (4 UTF-8 bytes) but nondet content.
# This test will pass when string concat tracks content.
a: str = "α"
b: str = "β"
c: str = a + b
assert len(c) == 4
assert c == "αβ"
