# Native SMT-String back-end: an exact len() relation over a *produced* string
# (concat via +, +=, f-string, * repeat; and a directional bound for replace)
# must verify. Regression for the int2bv length-family perf cliff: a produced
# string's length is read as int2bv(str.len(<term>)), and cvc5 cannot relate
# `int2bv(a+b)` to `bvadd(int2bv a, int2bv b)` for an exact relation, so
# `len(s + t) == len(s) + len(t)` timed out. Producers now carry an exact (or
# sound directional, for replace) length hint via a fresh aliased symbol.
s = nondet_string()
t = nondet_string()

# concat (+)
assert len(s + t) == len(s) + len(t)
assert len(s + "Z") == len(s) + 1

# += (augmented concat)
n = len(s)
s2 = s
s2 += "ab"
assert len(s2) == n + 2

# f-string with symbolic-length parts
assert len(f"{s}{t}") == len(s) + len(t)

# * repeat by a constant
assert len(t * 3) == len(t) * 3

# replace: equal-length is exact; size-changing is a sound directional bound
assert len(t.replace("a", "b")) == len(t)
assert len(t.replace("a", "bb")) >= len(t)
assert len(t.replace("ab", "c")) <= len(t)
