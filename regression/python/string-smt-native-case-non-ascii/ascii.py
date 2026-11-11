# ASCII characters stay exact.
s = nondet_str()
if s == "aB1z":
    assert s.upper() == "AB1Z"
    assert s.swapcase() == "Ab1Z"
    assert s.title() == "Ab1Z"
