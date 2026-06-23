# PLR / CPython str.replace with an EMPTY pattern inserts the replacement
# at every char boundary (before each char and at the end), left to right,
# limited by the optional count.
assert "a".replace("", "x") == "xax"
assert "ab".replace("", "-") == "-a-b-"
assert "abc".replace("", "-") == "-a-b-c-"
assert "abc".replace("", "-", 2) == "-a-bc"
assert "".replace("", "x") == "x"
assert "".replace("x", "y") == ""
# non-empty pattern still works
assert "hello".replace("l", "L") == "heLLo"
