# Lexicographic ordering of symbolic strings and of lists of strings.
s = nondet_str()
t = nondet_str()
if s == "Abb" and t == "Abc":
    assert s <= s
    assert s < t
    assert not (t < s)
    assert "1b" < s

c = nondet_int()
s = "ab" if c > 0 else "ax"
t = "ac" if c > 0 else "ay"
assert [s] < [t]
assert [t, s] > [t, "a"]
assert not ([s] >= [t])
assert ["ab", s] <= ["ab", s]
