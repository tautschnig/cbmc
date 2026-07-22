# Native string-id handles: list[str]/set[str] MEMBERSHIP reads elements
# through the strtab denotation (strtab(h)), so `in` compares string
# VALUES -- raw handle bits would wrongly refute membership whenever the
# probe and the stored element differ as handles (found by doc-review
# sweep, not the corpus: sets are list-backed, so both shapes share the
# one membership scan).
xs = ["a", "b"]
assert "a" in xs
assert "c" not in xs

s = {"a", "b"}
assert "a" in s
assert "z" not in s
assert len(s) == 2
