x = nondet_str()
y = "pre" + x + "post"
if y == "prefoopost":
    assert x == "fo"
