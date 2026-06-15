# Native SMT-String str.split: constant subjects fold to a precise list;
# symbolic subjects yield a sound length-bounded nondet list.
assert "a,b,c".split(",")[0] == "a"
assert "a,b,c".split(",")[2] == "c"
assert len("a,b,c".split(",")) == 3
assert len("a b  c".split()) == 3          # whitespace mode (runs collapsed)
assert "a,b,c".split(",", 1)[1] == "b,c"   # maxsplit

# Symbolic subject: sound nondet list (length non-negative and bounded).
s = input()
assert len(s.split(",")) >= 0
