# find/count/index with start and end positions, as in slice notation; a
# symbolic position must not be taken as 0, and an end of -1 is not "no end".
s = nondet_str()
k = nondet_int()
if k == 1:
    assert "abc".find("a", k) == -1
    assert "abc".find("c", 0, -1) == -1
    assert "abc".count("", 5) == 0
    assert "abc".count("", 2, 1) == 0
    assert "abc".count("") == 4
    assert "Bb".strip("b") == "B"
try:
    c = s.count(""[k], k)
    assert False
except IndexError:
    pass
if s == "xyz":
    try:
        n = s.index("q")
        assert False
    except ValueError:
        pass
