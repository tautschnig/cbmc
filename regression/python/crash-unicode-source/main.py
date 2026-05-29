# String concatenation content tracking — not a Unicode issue.
# "α" + "β" has 2 code points; len() in Python returns the
# code-point count, not the UTF-8 byte count.
a: str = "α"
b: str = "β"
c: str = a + b
assert len(c) == 2
assert c == "αβ"
