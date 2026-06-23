# re.escape soundness + precision. The library stub returned "" (an empty
# regex matches EVERYTHING), so `re.match(re.escape("abc"), "zzz")` wrongly
# verified as a match -- a false proof (CPython returns None). re.escape(x) is
# now modelled soundly: a constant x is escaped properly (so the downstream
# regex matches x literally); a symbolic x falls back to a sound nondet string.
import re

# Precision (constant): escaped special chars match literally, not as regex.
assert re.match(re.escape("a.c"), "a.c") is not None   # '.' is literal here
assert re.match(re.escape("a.c"), "axc") is None       # so 'x' does NOT match
assert re.fullmatch(re.escape("a+b"), "a+b") is not None
assert re.fullmatch(re.escape("a+b"), "aaab") is None  # '+' is literal, not quantifier

# Soundness (was a false proof): a non-matching subject must NOT verify.
assert re.match(re.escape("abc"), "zzz") is None
