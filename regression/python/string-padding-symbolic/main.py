# ljust/rjust/center of symbolic strings with the refined string solver.
s = nondet_str()
k = nondet_int()
if s == "ab" and k == 5:
    assert s.rjust(5) == "   ab"
    assert s.ljust(4, "*") == "ab**"
    assert s.center(5) == "  ab "
    assert s.center(6, "-") == "--ab--"
    assert s.center(5, "-") == "--ab-"
    assert s.rjust(1) == "ab"
    assert s.ljust(k) == "ab   "
    assert s.center(k) == "  ab "
    assert s.rjust(-3) == "ab"
t = nondet_str()
assert len(t.rjust(10)) >= 10
