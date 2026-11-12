# Refined strings hold UTF-8 bytes: each of these assertions is false for
# CPython, and they verified when positions and lengths counted bytes.
__assert_k = nondet_int(); s = nondet_str()
if s == "\u00e9a ":
    n = 0
    for c in s:
        n += 1
    assert __assert_k != 0 or (n == 4)
    found = False
    for c in s:
        if c == "\u00e9":
            found = True
    assert __assert_k != 1 or (not found)
    assert __assert_k != 2 or (len(s) == 4)
    assert __assert_k != 3 or (s.find(" ") == 3)
    assert __assert_k != 4 or (s.index("a") == 2)
    assert __assert_k != 5 or (s[1:] == "\u00a9a ")
    assert __assert_k != 6 or (s.count("") == 5)
    assert __assert_k != 7 or (s.count("a", 2) == 1)
    assert __assert_k != 8 or (not s.startswith("a", 1))
    assert __assert_k != 9 or (s.rjust(4) == "\u00e9a ")
    try:
        s[3]
    except IndexError:
        assert __assert_k != 10 or (False)
