s = nondet_str()
if s == "a":
    assert "ab".removeprefix(s) == "ab"
