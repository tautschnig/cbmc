# Native SMT-String back-end: count / rfind / rindex fold for constant
# subjects; symbolic subjects are modelled with str.replace_all (count) and
# constraints on the last occurrence (rfind; SMT-LIB has no
# str.last_indexof).
assert "hello".count("l") == 2
assert "hello".rfind("l") == 3
assert "hello".rindex("l") == 3
assert "banana".count("a") == 3
assert "abcabc".rfind("bc") == 4

# Bounds on a symbolic subject.
s = input()
assert s.count("a") >= 0
assert s.rfind("a") >= -1
