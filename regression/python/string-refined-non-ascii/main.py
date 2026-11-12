# Lengths and positions of refined strings count characters.
s = nondet_str()
assert len(s) == len(s)
if s == "_\u00e90":
    assert len(s) == 3
    assert s.find("z") == -1
if s == "\u20ac":
    assert len(s) == 1
    assert ord(s) == 8364
t = nondet_str()
if t == "ab c":
    assert t.find(" ") == 2
    assert t[1:3] == "b "
    assert t.count("") == 5
u = nondet_string(3)
assert len(u) == 3


def pad5(i: int) -> str:
    return f"{i:05}"


assert len(pad5(42)) == 5
assert len(pad5(1234567)) == 7
