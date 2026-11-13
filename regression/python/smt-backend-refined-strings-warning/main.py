s = nondet_str()
if s == "ab":
    assert len(s + "c") == 3
