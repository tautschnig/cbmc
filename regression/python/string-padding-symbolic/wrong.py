# Each of these assertions is false.
__assert_k = nondet_int(); s = nondet_str()
k = nondet_int()
if s == "ab" and k == 5:
    assert __assert_k != 0 or (s.rjust(5) != "   ab")
    assert __assert_k != 1 or (s.ljust(4, "*") != "ab**")
    assert __assert_k != 2 or (s.center(5) != "  ab ")
    assert __assert_k != 3 or (s.center(6, "-") != "--ab--")
    assert __assert_k != 4 or (s.center(5, "-") != "--ab-")
    assert __assert_k != 5 or (s.rjust(1) != "ab")
    assert __assert_k != 6 or (s.ljust(k) != "ab   ")
    assert __assert_k != 7 or (s.center(k) != "  ab ")
    assert __assert_k != 8 or (s.rjust(-3) != "ab")
