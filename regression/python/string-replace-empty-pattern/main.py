# PLR / CPython str.replace with an EMPTY pattern inserts the replacement
# at every char boundary (before each char and at the end), left to right,
# limited by the optional count.
# Constant cases (precise content):
assert "a".replace("", "x") == "xax"
assert "ab".replace("", "-") == "-a-b-"
assert "abc".replace("", "-") == "-a-b-c-"
assert "abc".replace("", "-", 2) == "-a-bc"
assert "".replace("", "x") == "x"
assert "".replace("x", "y") == ""
assert "hello".replace("l", "L") == "heLLo"


# Symbolic source: the result LENGTH is exact = len(s)*(1+len(new)) + len(new).
def sym() -> None:
    s = nondet_str()
    __ESBMC_assume(len(s) == 1)
    assert len(s.replace("", "x")) == 3

    t = nondet_str()
    __ESBMC_assume(len(t) == 2)
    assert len(t.replace("", "yz")) == 8


sym()
