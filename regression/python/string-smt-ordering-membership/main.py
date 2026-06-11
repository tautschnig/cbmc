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

# length (str.len) and find/index (str.indexof) query lowerings.
assert len(u) == 3
assert u.find("b") == 1
assert u.find("z") == -1
assert u.index("c") == 2

# Producing-op values: concatenation via a single str.++ constraint, and
# subscript via str.substr -- precise for symbol operands.
a = nondet_string(2)
assume(a == "ab")
b = nondet_string(2)
assume(b == "cd")
c = a + b
assert c == "abcd"
assert len(c) == 4
assert "bc" in c
assert c[2] == "c"
assert u[1] == "b"
