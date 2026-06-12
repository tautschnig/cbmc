# Native SMT-String backend: re match/search/fullmatch return a real
# Match-or-None reflecting the SMT regex decision (not always-Match), for
# BOTH the re.compile(p).method(s) and the module-level re.method(p, s) forms.
# (Precise under --cvc5 --python-smt-strings; the stub annotates these as
# returning "Match | None", matching CPython.)
import re

# re.compile(...).method(...)
assert re.compile("abc").match("abcxyz") is not None
assert re.compile("xyz").match("abcxyz") is None
assert re.compile("bcx").search("abcxyz") is not None
assert re.compile("zzz").search("abcxyz") is None
assert re.compile("abc").fullmatch("abc") is not None
assert re.compile("abc").fullmatch("abcd") is None

# Module-level re.method(p, s)
assert re.match("abc", "abcxyz") is not None
assert re.match("xyz", "abcxyz") is None
assert re.search("bcx", "abcxyz") is not None
assert re.search("zzz", "abcxyz") is None
assert re.fullmatch("abc", "abc") is not None
assert re.fullmatch("abc", "abcd") is None
