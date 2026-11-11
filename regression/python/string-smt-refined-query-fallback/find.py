# s.find("q") is -1 for most s: this assertion fails.
s = nondet_str()
assert s.find("q") == 0
