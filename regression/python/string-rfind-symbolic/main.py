s = nondet_string(5)
assume(s == "abcba")
# rfind returns the highest index; find the lowest
assert s.rfind("b") == 3
assert s.rfind("a") == 4
assert s.find("b") == 1
# absent substring yields -1
assert s.rfind("z") == -1
# rindex agrees with rfind when present
assert s.rindex("b") == 3
