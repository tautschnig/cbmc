# Native SMT-String back-end: removeprefix/removesuffix and the padding
# methods with a constant fill character.
s = nondet_str()
t = nondet_str()
k = nondet_int()
if s == "ab" and t == "-5" and k == 5:
    assert s.removeprefix("a") == "b"
    assert s.removeprefix("b") == "ab"
    assert s.removesuffix("b") == "a"
    assert s.removesuffix("") == "ab"
    assert s.ljust(k) == "ab   "
    assert s.rjust(k, "*") == "***ab"
    assert s.center(k) == "  ab "
    assert s.center(k - 1, ".") == ".ab."
    assert s.ljust(1) == "ab"
    assert t.zfill(k) == "-0005"
    assert s.zfill(k) == "000ab"
    assert s.center(7, "-") == "---ab--"
