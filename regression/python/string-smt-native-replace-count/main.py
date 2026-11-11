s = nondet_str()
o = nondet_str()
n = nondet_str()
k = nondet_int()
if s == "abab" and o == "ab" and n == "x" and k == 1:
    assert s.replace(o, n, k) == "xab"
    assert s.replace(o, n, 0) == "abab"
    assert s.replace(o, n, 2) == "xx"
    assert s.replace(o, n, 5) == "xx"
    assert s.replace(o, n, -1) == "xx"
    assert s.replace(o, n, k - 3) == "xx"
    assert s.replace("", n, k) == "xabab"
    assert s.replace("", n, 3) == "xaxbxab"
    assert s.replace("", "-", 9) == "-a-b-a-b-"
    assert s.replace("b", "", k + 1) == "aa"
    assert s.replace("ab", "ab!", 2) == "ab!ab!"
    assert "aaa".replace(o, n, k) == "aaa"
    assert s.replace(o, n, 12) == "xx"
