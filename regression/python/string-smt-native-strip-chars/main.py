# Native SMT-String strip/lstrip/rstrip with an explicit char-set argument,
# on symbolic subjects. Modelled by the same maximal decomposition as the
# whitespace form: s = p ++ r ++ q with p,q composed only of `chars` and r's
# boundary characters not in `chars`.
s = input()

# Maximality holds for ANY input: the result has no leading/trailing stripped
# char (sound regardless of subject).
assert not s.strip("x").startswith("x")
assert not s.strip("x").endswith("x")
assert not s.lstrip("z").startswith("z")

# Precise on assume-pinned subjects.
assume(s == "www.ex.com")
assert s.strip("cmowz.") == "ex"
