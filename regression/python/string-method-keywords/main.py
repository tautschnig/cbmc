# Keyword arguments of string methods are passed in their positions.
s = nondet_str()
if s == "a,b,c":
    assert s.split(sep=",") == ["a", "b", "c"]
    assert s.split(",", maxsplit=1) == ["a", "b,c"]
    assert s.rsplit(",", maxsplit=1) == ["a,b", "c"]
    assert s.splitlines(keepends=True) == ["a,b,c"]
    assert "a\nb".splitlines(keepends=True) == ["a\n", "b"]
    assert s.split(maxsplit=0) == ["a,b,c"]
    assert "x y z".split(maxsplit=1) == ["x", "y z"]
    assert "ab\tc".expandtabs(tabsize=4) == "ab  c"
    assert "a b".encode(encoding="ascii") == b"a b"
