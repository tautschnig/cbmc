# Native SMT-String back-end: count / rfind / rindex fold for constant
# subjects; symbolic subjects fall back to a sound nondet over-approximation
# (no SMT-LIB str.last_indexof, and count has no native primitive).
assert "hello".count("l") == 2
assert "hello".rfind("l") == 3
assert "hello".rindex("l") == 3
assert "banana".count("a") == 3
assert "abcabc".rfind("bc") == 4

# Sound bounds on a symbolic subject.
s = input()
assert s.count("a") >= 0
assert s.rfind("a") >= -1
