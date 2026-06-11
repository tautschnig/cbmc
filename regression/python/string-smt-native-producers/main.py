# Plan A: native SMT-String backend producing ops (concat/subscript/slice/
# replace) -- all lower to native str.* with results carrying their own length.
a = nondet_string(2)
assume(a == "ab")
b = nondet_string(2)
assume(b == "cd")

c = a + b
assert c == "abcd"
assert len(c) == 4
assert "bc" in c
assert c.startswith("ab")
assert c[2] == "c"

s = nondet_string(4)
assume(s == "abcd")
assert s[1:3] == "bc"
assert s[0] == "a"
assert s[-1] == "d"

r = nondet_string(3)
assume(r == "aba")
assert r.replace("a", "X") == "XbX"
