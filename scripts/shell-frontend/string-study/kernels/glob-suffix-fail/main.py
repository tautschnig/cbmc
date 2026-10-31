f = nondet_str()
kind = "text" if f.endswith(".txt") else "other"
if kind == "text":
    assert f != "notes.txt"
