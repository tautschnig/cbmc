# Native SMT-String back-end: slice bounds of a symbolic string are clamped
# into [0, len(s)] and an empty range yields "".
s = nondet_string(5)
k = nondet_int()
if s == "hello" and k == -9:
    assert s[k:2] == "he"
    assert s[k:] == "hello"
    assert s[:k] == ""
    assert s[1:k] == ""
if s == "hello" and k == 9:
    assert s[k:] == ""
    assert s[1:k] == "ello"
if s == "hello" and k == 3:
    assert s[k:1] == ""
    assert s[-2:] == "lo"
