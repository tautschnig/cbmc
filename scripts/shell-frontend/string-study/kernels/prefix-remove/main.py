x = nondet_str()
if x.startswith("--"):
    y = x.removeprefix("--")
    assert len(y) == len(x) - 2
