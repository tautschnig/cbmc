# The DEFAULT (refined-string) backend decides re.match / re.search /
# re.fullmatch precisely for a CONSTANT pattern + CONSTANT subject (no SMT
# String solver / --cvc5 needed): a match returns Match(), a proven no-match
# returns None. (Symbolic subjects stay a sound nondet Match-or-None.)
import re

# Hits -> Match().
assert re.match("abc", "abcdef") is not None
assert re.match("[0-9]+", "12345") is not None
assert re.search("123", "abc123") is not None
assert re.fullmatch("a.*", "abc") is not None
assert re.fullmatch("(a|b)+", "abba") is not None

# Misses -> None.
assert re.match("xyz", "abcdef") is None
assert re.fullmatch("[a-z]+", "12") is None
assert re.search("zzz", "abc") is None
assert re.fullmatch("abc", "abcd") is None

# Inline-flag patterns (IGNORECASE / DOTALL via "(?i)" / "(?s)") are precise
# on the default backend too (the constant-fold matcher honours the prefix).
assert re.match("(?i)abc", "ABC") is not None
assert re.fullmatch("(?i)[a-z]+", "HELLO") is not None
assert re.match("(?i)abc", "abd") is None
