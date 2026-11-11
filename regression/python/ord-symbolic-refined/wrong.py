# Each of these assertions is false; "aba" made ord() return without
# raising TypeError.
s = nondet_str()
if s == "a":
    assert ord(s) != 97
if s == "é":
    assert ord(s) != 233
if s == "€":
    assert ord(s) != 8364
if s == "😀":
    assert ord(s) != 128512
if s == "aba" or s == "" or s == "ab":
    try:
        r = ord(s)
        assert False
    except TypeError:
        assert False
