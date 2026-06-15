import re

# Inline regex flag groups (?i) / (?s) are standard regex syntax and are
# modelled precisely by the (language-neutral) SMT regex translator -- no
# Python `flags` value reaches the back-end. IGNORECASE folds ASCII letters to
# either case; DOTALL makes '.' match a newline.
assert re.search("(?i)abc", "xABCy") is not None
assert re.fullmatch("(?i)[a-c]+", "AbC") is not None
assert re.fullmatch("(?s)a.c", "a\nc") is not None
assert re.compile("(?i)abc").search("xABCy") is not None

# Without the inline flag, matching is case-sensitive / newline-excluding.
assert re.search("abc", "ABC") is None
assert re.fullmatch("a.c", "a\nc") is None
