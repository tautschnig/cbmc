s = nondet_string(2)
assume(s == "ab")
t = nondet_string(2)
assume(t == "ac")

# Ordering: precise under the SMT-String backend (the refined backend
# cannot close this -- compare_to's existential witness is not instantiated).
assert s < t
assert t > s
assert s <= t
assert not (t < s)

# Membership over a symbol operand: precise via leaf backing + str.contains.
u = nondet_string(3)
assume(u == "abc")
assert "b" in u
assert "z" not in u
assert u.startswith("a")
assert u == "abc"
assert u != "xyz"
