a = nondet_str()
if a.startswith("--") and "=" in a:
    name, sep, value = a.partition("=")
    assert value != "on"
