# Plan A: native SMT-String backend (--python-smt-strings-native).
# Query ops over native SMT String values.
s = nondet_string(3)
assume(s == "abc")
t = nondet_string(3)
assume(t == "abd")

assert s == "abc"
assert s != "xyz"
assert s < t
assert t > s
assert s <= t
assert len(s) == 3
assert "b" in s
assert "z" not in s
assert s.startswith("a")
assert s.endswith("c")
assert s.find("b") == 1
assert s.find("z") == -1
