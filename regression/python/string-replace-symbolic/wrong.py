s = nondet_str()
n = nondet_str()
k = nondet_int()
if s == "abab" and n == "x" and k == 1:
    assert not (
        s.replace("ab", n, k) == "xab" and
        s.replace("ab", n, 0) == "abab" and
        s.replace("ab", n) == "xx" and
        s.replace("ab", n, k - 3) == "xx" and
        s.replace("", n, k) == "xabab" and
        s.replace("", n, 3) == "xaxbxab" and
        s.replace("b", "", k + 1) == "aa" and
        s.replace("c", n) == "abab"
    )
