# Native SMT-String back-end: a Python str is a sequence of code points, so a
# non-ASCII constant is one SMT-LIB character per code point, not per UTF-8
# byte.
s = nondet_str()
if s == "é":
    assert len(s) == 1
    assert ord(s) == 233
if s == "日本":
    assert s[1] == "本" and len(s) == 2
if s == "x":
    t = s + "é"
    assert t.find("é") == 1 and len(t) == 2
