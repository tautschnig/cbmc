# s[-9:2] of "hello" is "he": this assertion fails.
s = nondet_string(5)
k = nondet_int()
if s == "hello" and k == -9:
    assert s[k:2] != "he"
